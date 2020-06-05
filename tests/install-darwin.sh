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
        if [ -e "$file" ]; then
            cat "$file" | grep -v nix-profile > "$file.next"
            mv "$file.next" "$file"
        fi
    done

    for i in $(seq 1 $(sysctl -n hw.ncpu)); do
        sudo /usr/bin/dscl . -delete "/Users/nixbld$i" || true
    done
    sudo /usr/bin/dscl . -delete "/Groups/nixbld" || true

    sudo rm -rf /etc/nix \
         /nix \
         /var/root/.nix-profile /var/root/.nix-defexpr /var/root/.nix-channels \
         "$HOME/.nix-profile" "$HOME/.nix-defexpr" "$HOME/.nix-channels"
}

verify() {
    set +e
    output=$(echo "nix-shell -p bash --run 'echo toow | rev'" | bash -l)
    set -e

    test "$output" = "woot"
}

scratch=$(mktemp -d -t tmp.XXXXXXXXXX)
function finish {
    rm -rf "$scratch"
}
trap finish EXIT

# First setup Nix
cleanup
curl -o install https://nixos.org/nix/install
yes | bash ./install
verify


(
    set +e
    (
        echo "cd $(pwd)"
        echo nix-build ./release.nix -A binaryTarball.x86_64-darwin
    ) | bash -l
    set -e
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

    echo -n "" | ./install
    verify
    cleanup

    sudo mkdir -p /nix/store
    sudo touch /nix/store/.silly-hint
    echo -n "" | ALLOW_PREEXISTING_INSTALLATION=true ./install
    verify
    test -e /nix/store/.silly-hint

    cleanup
)
