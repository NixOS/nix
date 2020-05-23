#!/bin/sh
set -e

root_disk() {
    diskutil info -plist /
}

apfs_volumes_for() {
    disk=$1
    diskutil apfs list -plist "$disk"
}

disk_identifier() {
    xpath "/plist/dict/key[text()='ParentWholeDisk']/following-sibling::string[1]/text()" 2>/dev/null
}

volume_list_true() {
    key=$1
    xpath "/plist/dict/array/dict/key[text()='Volumes']/following-sibling::array/dict/key[text()='$key']/following-sibling::true[1]" 2> /dev/null
}

volume_get_string() {
    key=$1 i=$2
    xpath "/plist/dict/array/dict/key[text()='Volumes']/following-sibling::array/dict[$i]/key[text()='$key']/following-sibling::string[1]/text()" 2> /dev/null
}

find_nix_volume() {
    disk=$1
    i=1
    volumes=$(apfs_volumes_for "$disk")
    while true; do
        name=$(echo "$volumes" | volume_get_string "Name" "$i")
        if [ -z "$name" ]; then
            break
        fi
        case "$name" in
            [Nn]ix*)
                echo "$name"
                break
                ;;
        esac
        i=$((i+1))
    done
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
    disk=$1
    #      list vols on disk | get value of Filevault key | value is true
    apfs_volumes_for "$disk" | volume_list_true FileVault | grep -q true
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
        /System/Library/Filesystems/apfs.fs/Contents/Resources/apfs.util -B || true
        if ! test_nix; then
            sudo mkdir -p /nix 2>/dev/null || true
        fi
        if ! test_nix; then
            echo "error: failed to bootstrap /nix; if a reboot doesn't help," >&2
            suggest_report_error
            exit 1
        fi
    fi

    disk=$(root_disk | disk_identifier)
    volume=$(find_nix_volume "$disk")
    if [ -z "$volume" ]; then
        echo "Creating a Nix Store volume..." >&2

        if test_filevault_in_use "$disk"; then
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
        printf "\$a\nLABEL=%s /nix apfs rw,nobrowse\n.\nwq\n" "$label" | EDITOR=ed sudo vifs
    fi
}

main "$@"
