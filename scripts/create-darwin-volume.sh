#!/bin/sh
set -e

# make it easy to play w/ 'Case-sensitive APFS'
readonly NIX_VOLUME_FS="${NIX_VOLUME_FS:-APFS}"

readonly NIX_VOLUME_MOUNTD="${NIX_VOLUME_MOUNTD:-/Library/LaunchDaemons/org.nixos.darwin-store.plist}"

# i.e., "disk1"
root_disk_identifier() {
    /usr/sbin/diskutil info -plist / | xmllint --xpath "/plist/dict/key[text()='ParentWholeDisk']/following-sibling::string[1]/text()" -
}

volume_uuid(){
    /usr/sbin/diskutil info -plist "$1" | xmllint --xpath "(/plist/dict/key[text()='VolumeUUID']/following-sibling::string/text())" -
}

find_nix_volume() {
    /usr/sbin/diskutil apfs list -plist "$1" | xmllint --xpath "(/plist/dict/array/dict/key[text()='Volumes']/following-sibling::array/dict/key[text()='Name']/following-sibling::string[starts-with(translate(text(),'N','n'),'nix')]/text())[1]" - 2>/dev/null || true
}

test_fstab() {
    /usr/bin/grep -q "/nix apfs rw" /etc/fstab 2>/dev/null
}

test_nix_symlink() {
    [ -L "/nix" ] || /usr/bin/grep -q "^nix." /etc/synthetic.conf 2>/dev/null
}

test_synthetic_conf() {
    /usr/bin/grep -q "^nix$" /etc/synthetic.conf 2>/dev/null
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
    test -f "$NIX_VOLUME_MOUNTD"
}

test_filevault_in_use() {
    /usr/bin/fdesetup isactive >/dev/null
}

# use after error msg for conditions we don't understand
suggest_report_error(){
    # ex "error: something sad happened :(" >&2
    echo "       please report this @ https://github.com/nixos/nix/issues" >&2
}

generate_mount_command(){
    if test_filevault_in_use; then
        printf "    <string>%s</string>\n" /bin/sh -c '/usr/bin/security find-generic-password -a "Nix" -w | /usr/sbin/diskutil apfs unlockVolume "Nix" -mountpoint /nix -stdinpassphrase'
    else
        printf "    <string>%s</string>\n" /usr/sbin/diskutil mount -mountPoint /nix "Nix"
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

# $1=<volume name> $2=<volume uuid>
prepare_darwin_volume_password(){
    password="$(/usr/bin/xxd -l 32 -p -c 256 /dev/random)"
    sudo /usr/bin/security -i <<EOF
add-generic-password -a "$1" -s "$2" -l "$1 encryption password" -D "Encrypted volume password" -j "Added automatically by the Nix installer for use by $NIX_VOLUME_MOUNTD" -w "$password" -T /System/Library/CoreServices/APFSUserAgent -T /System/Library/CoreServices/CSUserAgent -T /usr/bin/security "/Library/Keychains/System.keychain"
EOF
    # Notes:
    # 1) system is in some sense less secure than user keychain... (it's
    # possible to read the password for decrypting the keychain) but
    # the user keychain appears to be available too late. As far as I
    # can tell, the file with this password (/var/db/SystemKey) is
    # inside the FileVault envelope. If that isn't true, it may make
    # sense to store the password inside the envelope?
    #
    # 2) At some point it would be ideal to have a small binary to serve
    # as the daemon itself, and for it to replace /usr/bin/security here.
    #
    # 3) *UserAgent exemptions should let the system seamlessly supply the
    # password if noauto is removed from fstab entry. This is intentional;
    # the user will hopefully look for help if the volume stops mounting,
    # rather than failing over into subtle race-condition problems.

    builtin printf "$password"
}

main() {
    (
        echo ""
        echo "     ------------------------------------------------------------------ "
        echo "    | This installer will create a volume for the nix store and        |"
        echo "    | configure it to mount at /nix.  Follow these steps to uninstall. |"
        echo "     ------------------------------------------------------------------ "
        echo ""
        echo "  1. Remove the entry from fstab using 'sudo vifs'"
        echo "  2. Run 'sudo launchctl bootout system/org.nixos.darwin-store'"
        echo "  3. Remove $NIX_VOLUME_MOUNTD"
        echo "  4. Destroy the data volume using 'diskutil apfs deleteVolume'"
        echo "  5. Remove the 'nix' line from /etc/synthetic.conf (or the file)"
        echo ""
    ) >&2

    if test_nix_symlink; then
        echo "error: /nix is a symlink, please remove it and make sure it's not in synthetic.conf (in which case a reboot is required)" >&2
        echo "  /nix -> $(readlink "/nix")" >&2
        exit 2
    fi

    if ! test_synthetic_conf; then
        echo "Configuring /etc/synthetic.conf..." >&2
        # TODO: technically /etc/synthetic.d/nix is supported in Big Sur+
        # but handling both takes even more code...
        echo nix | /usr/bin/sudo /usr/bin/tee -a /etc/synthetic.conf
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
            /usr/bin/sudo /bin/mkdir -p /nix 2>/dev/null || true
        fi
        if ! test_nix; then
            echo "error: failed to bootstrap /nix; if a reboot doesn't help," >&2
            suggest_report_error
            exit 1
        fi
    fi

    disk="$(root_disk_identifier)"
    volume=$(find_nix_volume "$disk") # existing volname starting w/ "nix"?
    if [ -z "$volume" ]; then
        volume="Nix"    # otherwise use default
        create_volume=1
    fi
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
        printf "\$a\nLABEL=%s /nix apfs rw,noauto,nobrowse\n.\nwq\n" "$label" | EDITOR=/bin/ed /usr/bin/sudo /usr/sbin/vifs
    fi

    if [ -n "$create_volume" ]; then
        echo "Creating a Nix volume..." >&2

        /usr/bin/sudo /usr/sbin/diskutil apfs addVolume "$disk" "$NIX_VOLUME_FS" "$volume" -mountpoint /nix
        new_uuid="$(volume_uuid "$volume")"

        if [ "$INSTALL_MODE" = "no-daemon" ]; then # exported by caller
            # TODO: is there a better way to do this?
            /usr/bin/sudo /usr/sbin/chown "$USER:admin" /nix
        fi

        if test_filevault_in_use; then
            prepare_darwin_volume_password "$volume" "$new_uuid" | /usr/bin/sudo /usr/sbin/diskutil apfs encryptVolume "$volume" -user disk -stdinpassphrase
        fi
    else
        echo "Using existing '$volume' volume" >&2
    fi

    if ! test_voldaemon; then
        echo "Configuring LaunchDaemon to mount '$volume'..." >&2
        generate_mount_daemon | /usr/bin/sudo /usr/bin/tee "$NIX_VOLUME_MOUNTD" >/dev/null

        /usr/bin/sudo /bin/launchctl bootstrap system "$NIX_VOLUME_MOUNTD"
    fi
}

main "$@"
