source common.sh

# Tests miscellaneous commands.

# Do all commands have help?
$nixenv --help | grep -q install
$nixstore --help | grep -q realise
$nixinstantiate --help | grep -q eval-only
$nixhash --help | grep -q base32

# Can we ask for the version number?
$nixenv --version | grep "$version"

# Usage errors.
$nixenv --foo 2>&1 | grep "no operation"
$nixenv -q --foo 2>&1 | grep "unknown flag"
