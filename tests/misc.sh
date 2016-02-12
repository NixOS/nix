source common.sh

# Tests miscellaneous commands.

# Do all commands have help?
#nix-env --help | grep -q install
#nix-store --help | grep -q realise
#nix-instantiate --help | grep -q eval
#nix-hash --help | grep -q base32

# Can we ask for the version number?
nix-env --version | grep "$version"

# Usage errors.
nix-env --foo 2>&1 | grep "no operation"
nix-env -q --foo 2>&1 | grep "unknown flag"

# Attribute path errors
nix-instantiate --eval -E '{}' -A '"x' 2>&1 | grep "missing closing quote in selection path"
nix-instantiate --eval -E '[]' -A 'x' 2>&1 | grep "should be a set"
nix-instantiate --eval -E '{}' -A '1' 2>&1 | grep "should be a list"
nix-instantiate --eval -E '{}' -A '.' 2>&1 | grep "empty attribute name"
nix-instantiate --eval -E '[]' -A '1' 2>&1 | grep "out of range"
