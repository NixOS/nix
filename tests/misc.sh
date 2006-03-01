source common.sh

# Tests miscellaneous commands.

# Do all commands have help?
$TOP/src/nix-env/nix-env --help | grep -q install
$TOP/src/nix-store/nix-store --help | grep -q realise
$TOP/src/nix-instantiate/nix-instantiate --help | grep -q eval-only
$TOP/src/nix-hash/nix-hash --help | grep -q base32

# Can we ask for the version number?
$TOP/src/nix-env/nix-env --version | grep "$version"
