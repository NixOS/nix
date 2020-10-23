#!/bin/sh
set -e

root_disk() {
    diskutil info -plist /
}

# i.e., "disk1"
root_disk_identifier() {
    diskutil info -plist / | xmllint --xpath "/plist/dict/key[text()='ParentWholeDisk']/following-sibling::string[1]/text()" -
}

find_nix_volume() {
    diskutil apfs list -plist "$1" | xmllint --xpath "(/plist/dict/array/dict/key[text()='Volumes']/following-sibling::array/dict/key[text()='Name']/following-sibling::string[starts-with(translate(text(),'N','n'),'nix')]/text())[1]" - 2>/dev/null || true
}

test_fstab() {
    grep -q "/nix apfs rw" /etc/fstab 2>/dev/null
}

test_nix_symlink() {
    [ -L "/nix" ] || grep -q "^nix." /etc/synthetic.conf 2>/dev/null
}

test_synthetic_conf() {
    grep -q "^nix$" /etc/synthetic.conf 2>/dev/null
}

# Create the paths defined in synthetic.conf, saving us a reboot.
create_synthetic_objects(){
    # Big Sur takes away the -B flag we were using and replaces it
    # with a -t flag that appears to do the same thing (but they
    # don't behave exactly the same way in terms of return values).
    # This feels a little dirty, but as far as I can tell the
    # simplest way to get the right one is to just throw away stderr
    # and call both... :]
    {
        /System/Library/Filesystems/apfs.fs/Contents/Resources/apfs.util -t || true # Big Sur
        /System/Library/Filesystems/apfs.fs/Contents/Resources/apfs.util -B || true # Catalina
    } >/dev/null 2>&1
}

test_nix() {
    test -d "/nix"
}

test_voldaemon() {
    test -f "/Library/LaunchDaemons/org.nixos.darwin-store.plist"
}

test_filevault_in_use() {
    fdesetup isactive >/dev/null
}

# use after error msg for conditions we don't understand
suggest_report_error(){
    # ex "error: something sad happened :(" >&2
    echo "       please report this @ https://github.com/nixos/nix/issues" >&2
}

generate_mount_command(){
    if test_filevault_in_use; then
        printf "    <string>%s</string>\n" /bin/sh -c '/usr/bin/security find-generic-password -l "Nix Volume" -a "Nix Volume" -s "Nix Volume" -w | /usr/sbin/diskutil apfs unlockVolume "Nix Volume" -mountpoint /nix -stdinpassphrase'
    else
        printf "    <string>%s</string>\n" /usr/sbin/diskutil mount -mountPoint /nix "Nix Volume"
    fi
}

generate_mount_daemon(){
    cat << EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple Computer//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <dict>
    <key>PathState</key>
    <dict>
      <key>/nix/var/nix</key>
      <false/>
    </dict>
  </dict>
  <key>Label</key>
  <string>org.nixos.darwin-store</string>
  <key>ProgramArguments</key>
  <array>
$(generate_mount_command)
  </array>
</dict>
</plist>
EOF
}

prepare_darwin_volume_password(){
    sudo /usr/bin/expect << 'EOF'
log_user 0
set PASSPHRASE [exec /usr/bin/ruby -rsecurerandom -e "puts SecureRandom.hex(32)"]

# Cargo culting: people recommend this; not sure how necessary
set send_slow {1 .1}
spawn /usr/bin/sudo /usr/bin/security add-generic-password -a "Nix Volume" -s "Nix Volume" -D "Nix Volume password" -U -w
expect {
    "password data for new item: " {
        send -s -- "$PASSPHRASE\r"
        expect "retype password for new item: " {
            send -s -- "$PASSPHRASE\r"
        }
    }
}
expect eof
send_user "$PASSPHRASE"
EOF
}

main() {
    (
        echo ""
        echo "     ------------------------------------------------------------------ "
        echo "    | This installer will create a volume for the nix store and        |"
        echo "    | configure it to mount at /nix.  Follow these steps to uninstall. |"
        echo "     ------------------------------------------------------------------ "
        echo ""
        echo "  1. Remove /Library/LaunchDaemons/org.nixos.darwin-store.plist"
        echo "  2. Destroy the data volume using 'diskutil apfs deleteVolume'"
        echo "  3. Remove the 'nix' line from /etc/synthetic.conf or the file"
        echo ""
    ) >&2

    if test_nix_symlink; then
        echo "error: /nix is a symlink, please remove it and make sure it's not in synthetic.conf (in which case a reboot is required)" >&2
        echo "  /nix -> $(readlink "/nix")" >&2
        exit 2
    fi

    if ! test_synthetic_conf; then
        echo "Configuring /etc/synthetic.conf..." >&2
        echo nix | sudo tee -a /etc/synthetic.conf
        if ! test_synthetic_conf; then
            echo "error: failed to configure synthetic.conf;" >&2
            suggest_report_error
            exit 1
        fi
    fi

    if ! test_nix; then
        echo "Creating mountpoint for /nix..." >&2
        create_synthetic_objects # the ones we defined in synthetic.conf
        if ! test_nix; then
            sudo mkdir -p /nix 2>/dev/null || true
        fi
        if ! test_nix; then
            echo "error: failed to bootstrap /nix; if a reboot doesn't help," >&2
            suggest_report_error
            exit 1
        fi
    fi

    disk="$(root_disk_identifier)"
    volume=$(find_nix_volume "$disk") # existing volname starting w/ "nix"?
    volume="${volume:-Nix Volume}"    # otherwise use default

    # fstab used to be responsible for mounting the volume. Now the last
    # step adds a LaunchDaemon responsible for mounting. This is technically
    # redundant for mounting, but diskutil appears to pick up mount options
    # from fstab (and diskutil's support for specifying them directly is not
    # consistent across versions/subcommands), enabling us to specify mount
    # options by *label*.
    #
    # Being able to do all of this by label is helpful because it's a stable
    # identifier that we can know at code-time, letting us skirt some logistic
    # complexity that comes with doing this by UUID (which is stable, but not
    # known ahead of time) or special device name/path (which is not stable).
    if ! test_fstab; then
        echo "Configuring /etc/fstab..." >&2
        label=$(echo "$volume" | sed 's/ /\\040/g')
        # shellcheck disable=SC2209
        printf "\$a\nLABEL=%s /nix apfs rw,nobrowse\n.\nwq\n" "$label" | EDITOR=ed sudo vifs
    fi

    if [ -z "$volume" ]; then
        echo "Creating a Nix volume..." >&2

        if test_filevault_in_use; then
            # security program's flags won't let us both specify a keychain
            # and be prompted for a pw to add; two step workaround:
            # 1. add a blank pw to a keychain
            #    - system if daemon
            if [ "$INSTALL_MODE" = "daemon" ]; then # exported by caller
                # system is technically less secure than user... in theory we
                # could install the password in each user keychain, but we'd
                # need some ergonomic way to add users after install...
                sudo /usr/bin/security add-generic-password -a "Nix Volume" -s "Nix Volume" -D "Nix Volume password" "/Library/Keychains/System.keychain"
            #    - login (default) if single-user
            else
                sudo /usr/bin/security add-generic-password -a "Nix Volume" -s "Nix Volume" -D "Nix Volume password"
            fi
            # 2. add a password with the -U (update) flag and -w (prompt if last)
            #    flags, but specify no keychain; security will use the first it finds
            prepare_darwin_volume_password | sudo diskutil apfs addVolume "$disk" APFS 'Nix Volume' -mountpoint /nix -stdinpassphrase
        else
            sudo diskutil apfs addVolume "$disk" APFS 'Nix Volume' -mountpoint /nix
        fi
    else
        echo "Using existing '$volume' volume" >&2
    fi

    if ! test_voldaemon; then
        echo "Configuring LaunchDaemon to mount '$volume'..." >&2
        generate_mount_daemon | sudo tee /Library/LaunchDaemons/org.nixos.darwin-store.plist >/dev/null

        sudo launchctl load /Library/LaunchDaemons/org.nixos.darwin-store.plist

        sudo launchctl start org.nixos.darwin-store
    fi
}

main "$@"
