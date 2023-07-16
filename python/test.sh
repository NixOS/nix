SCRIPT_DIR=$( cd -- "$( dirname -- "${BASH_SOURCE[0]}" )" &> /dev/null && pwd )

source "$TEST_SCRIPTS"/share/bash/nix-test.sh

python "$SCRIPT_DIR"/tests.py
