#!/bin/sh

set -eux

cleanup() {
    PLIST="/Library/LaunchDaemons/org.nixos.nix-daemon.plist"
    if sudo launchctl list | grep -q nix-daemon; then
        sudo launchctl unload "$PLIST"
    fi

    if [ -f "$PLIST" ]; then
        sudo rm /Library/LaunchDaemons/org.nixos.nix-daemon.plist
    fi

    profiles=(/etc/profile /etc/bashrc /etc/zshrc)
    for profile in "${profiles[@]}"; do
        if [ -f "${profile}.backup-before-nix" ]; then
            sudo mv "${profile}.backup-before-nix" "${profile}"
        fi
    done

    for file in ~/.bash_profile ~/.bash_login ~/.profile ~/.zshenv ~/.zprofile ~/.zshrc ~/.zlogin; do
        cat "$file" | grep -v nix-profile > "$file.next"
        mv "$file.next" "$file"
    done



    for i in $(seq 1 $(sysctl -n hw.ncpu)); do
        sudo /usr/bin/dscl . -delete "/Users/nixbld$i" || true
    done
    sudo /usr/bin/dscl . -delete "/Groups/nixbld" || true


    sudo rm -rf /etc/nix \
         /nix \
         /var/root/.nix-profile /var/root/.nix-defexpr /var/root/.nix-channels \
         "$USER/.nix-profile" "$USER/.nix-defexpr" "$USER/.nix-channels"
}

verify() {
    output=$(echo "nix-shell -p bash --run 'echo toow | rev'" | bash -l)
    test "$output" = "woot"
}

scratch=$(mktemp -d -t tmp.XXXXXXXXXX)
function finish {
    rm -rf "$scratch"
}
trap finish EXIT

# First setup Nix
cleanup
curl https://nixos.org/nix/install | bash
verify


(
    (
        echo "cd $(pwd)"
        echo nix-build ./release.nix -A binaryTarball.x86_64-darwin
    ) | bash -l
    cp ./result/nix-*.tar.bz2 $scratch/nix.tar.bz2
)

(
    cd $scratch
    tar -xf ./nix.tar.bz2

    cd nix-*

    set -eux

    cleanup

    yes | ./install
    verify

    cleanup

    yes | ./install
    verify

    cleanup
)
