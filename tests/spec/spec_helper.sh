# shellcheck shell=sh

# Defining variables and functions here will affect all specfiles.
# Change shell options inside a function may cause different behavior,
# so it is better to set them here.
# set -eu

# This callback function will be invoked only once before loading specfiles.
spec_helper_precheck() {
  # Available functions: info, warn, error, abort, setenv, unsetenv
  # Available variables: VERSION, SHELL_TYPE, SHELL_VERSION
  : minimum_version "0.28.1"
}

# This callback function will be invoked after a specfile has been loaded.
spec_helper_loaded() {
  :
}

shellspec_readfile() {
  eval "$1=\$(cat \"\$2\"; echo _); $1=\${$1%_}"
}

# This callback function will be invoked after core modules has been loaded.
spec_helper_configure() {
  # Available functions: import, before_each, after_each, before_all, after_all
  : import 'support/custom_matcher'
}

setup_ca() {
      export NIX_REMOTE=""
      export TEST_ROOT=$(mktemp -d /tmp/nix-test.XXXXXXXXXXXXXXXXXXXXXXXXX)
      . ./common.sh
      bash init.sh
      cd ca
}

setup() {
    export NIX_REMOTE=""
    export TEST_ROOT=$(mktemp -d /tmp/nix-test.XXXXXXXXXXXXXXXXXXXXXXXXX)
    . ./common.sh
    bash init.sh
    clearStore
    clearCache
}

setup_flakes() {
    export NIX_REMOTE=""
    export LC_ALL=C
    export TEST_ROOT=$(mktemp -d /tmp/nix-test.XXXXXXXXXXXXXXXXXXXXXXXXX)
    . ./common.sh
    bash init.sh
    clearStore
    clearCache
    cd flakes
}
