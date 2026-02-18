# shellcheck shell=bash

# Common setup for tectonix functional tests

set -eu -o pipefail

if [[ -z "${TECTONIX_COMMON_SH_SOURCED-}" ]]; then

TECTONIX_COMMON_SH_SOURCED=1

# Source the main test framework
source "$(dirname "${BASH_SOURCE[0]}")/../common.sh"

requireGit

# Create a test world repository with zones
create_test_world() {
    local dir="$1"

    # Initialize git repo
    git init "$dir"
    cd "$dir"

    # Create directory structure
    mkdir -p .meta
    mkdir -p areas/tools/dev
    mkdir -p areas/tools/tec
    mkdir -p areas/platform/core

    # Create manifest
    cat > .meta/manifest.json << 'MANIFEST_EOF'
{
    "//areas/tools/dev": { "id": "W-000001" },
    "//areas/tools/tec": { "id": "W-000002" },
    "//areas/platform/core": { "id": "W-000003" }
}
MANIFEST_EOF

    # Create zone files
    echo '{ }' > areas/tools/dev/zone.nix
    echo 'Dev zone README' > areas/tools/dev/README.md
    echo '{ }' > areas/tools/tec/zone.nix
    echo '{ }' > areas/platform/core/zone.nix
    echo 'Test World' > README.md

    # Configure git
    git config user.email "test@example.com"
    git config user.name "Test User"

    # Create sparse-checkout-roots so dirty zone detection works
    mkdir -p .git/info
    cat > .git/info/sparse-checkout-roots << 'SPARSE_EOF'
W-000001
W-000002
W-000003
SPARSE_EOF

    # Commit everything
    git add -A
    git commit -m "Initial commit"

    cd - > /dev/null
}

# Get the HEAD SHA of a repo
get_head_sha() {
    local dir="$1"
    git -C "$dir" rev-parse HEAD
}

# Evaluate a nix expression with tectonix settings
tectonix_eval() {
    local git_dir="$1"
    local git_sha="$2"
    local expr="$3"
    shift 3

    nix eval --raw \
        --extra-experimental-features 'nix-command' \
        --option tectonix-git-dir "$git_dir" \
        --option tectonix-git-sha "$git_sha" \
        "$@" \
        --expr "$expr"
}

# Evaluate with JSON output
tectonix_eval_json() {
    local git_dir="$1"
    local git_sha="$2"
    local expr="$3"
    shift 3

    nix eval --json \
        --extra-experimental-features 'nix-command' \
        --option tectonix-git-dir "$git_dir" \
        --option tectonix-git-sha "$git_sha" \
        "$@" \
        --expr "$expr"
}

# Expect a command to fail
expect_failure() {
    if "$@" 2>/dev/null; then
        echo "Expected command to fail: $*" >&2
        return 1
    fi
    return 0
}

fi # TECTONIX_COMMON_SH_SOURCED
