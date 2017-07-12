#!/bin/sh

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
    nix-build ./release.nix -A binaryTarball.x86_64-darwin
    cp ./result/nix-*.tar.bz2 $scratch/nix.tar.bz2
)

(
    cd $scratch
    tar -xf ./nix.tar.bz2

    cd nix-*

    set -eux

    cat ~/.profile | grep -v nix-profile > ~/.profile-next
    mv ~/.profile-next ~/.profile

    cleanup

    yes | ./install
    verify

    cleanup

    yes | ./install
    verify

    cleanup
)
