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

test_t2_chip_present(){
    # Use xartutil to see if system has a t2 chip.
    #
    # This isn't well-documented on its own; until it is,
    # let's keep track of knowledge/assumptions.
    #
    # Warnings:
    # - Don't search "xart" if porn will cause you trouble :)
    # - Other xartutil flags do dangerous things. Don't run them
    #   naively. If you must, search "xartutil" first.
    #
    # Assumptions:
    # - the "xART session seeds recovery utility"
    #   appears to interact with xartstorageremoted
    # - `sudo xartutil --list` lists xART sessions
    #   and their seeds and exits 0 if successful. If
    #   not, it exits 1 and prints an error such as:
    #   xartutil: ERROR: No supported link to the SEP present
    # - xART sessions/seeds are present when a T2 chip is
    #   (and not, otherwise)
    # - the presence of a T2 chip means a newly-created
    #   volume on the primary drive will be
    #   encrypted at rest
    # - all together: `sudo xartutil --list`
    #   should exit 0 if a new Nix Store volume will
    #   be encrypted at rest, and exit 1 if not.
    sudo xartutil --list >/dev/null 2>/dev/null
}

test_filevault_in_use() {
    fdesetup isactive >/dev/null
}

# use after error msg for conditions we don't understand
suggest_report_error(){
    # ex "error: something sad happened :(" >&2
    echo "       please report this @ https://github.com/nixos/nix/issues" >&2
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
    volume=$(find_nix_volume "$disk")
    if [ -z "$volume" ]; then
        echo "Creating a Nix Store volume..." >&2

        if test_filevault_in_use; then
            # TODO: Not sure if it's in-scope now, but `diskutil apfs list`
            # shows both filevault and encrypted at rest status, and it
            # may be the more semantic way to test for this? It'll show
            # `FileVault:                 No (Encrypted at rest)`
            # `FileVault:                 No`
            # `FileVault:                 Yes (Unlocked)`
            # and so on.
            if test_t2_chip_present; then
                echo "warning: boot volume is FileVault-encrypted, but the Nix store volume" >&2
                echo "         is only encrypted at rest." >&2
                echo "         See https://nixos.org/nix/manual/#sect-macos-installation" >&2
            else
                echo "error: refusing to create Nix store volume because the boot volume is" >&2
                echo "       FileVault encrypted, but encryption-at-rest is not available." >&2
                echo "       Manually create a volume for the store and re-run this script." >&2
                echo "       See https://nixos.org/nix/manual/#sect-macos-installation" >&2
                exit 1
            fi
        fi

        sudo diskutil apfs addVolume "$disk" APFS 'Nix Store' -mountpoint /nix
        volume="Nix Store"
    else
        echo "Using existing '$volume' volume" >&2
    fi

    if ! test_fstab; then
        echo "Configuring /etc/fstab..." >&2
        label=$(echo "$volume" | sed 's/ /\\040/g')
        # shellcheck disable=SC2209
        printf "\$a\nLABEL=%s /nix apfs rw,nobrowse\n.\nwq\n" "$label" | EDITOR=ed sudo vifs
    fi
}

main "$@"
