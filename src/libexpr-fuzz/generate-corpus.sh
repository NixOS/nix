#!/usr/bin/env bash
# Generate fuzzing corpus

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CORPUS_DIR="${SCRIPT_DIR}/corpus"
TESTS_DIR="${SCRIPT_DIR}/../../tests/functional"

echo "Generating fuzzing corpus..."
rm -rf "${CORPUS_DIR}"
mkdir -p "${CORPUS_DIR}"

counter=0

# Create corpus file
create() {
    cat > "${CORPUS_DIR}/${counter}-$1.nix"
    ((counter++))
}

# Literals
echo "" | create empty
echo "null" | create null
echo "true" | create bool-true
echo "false" | create bool-false
echo "42" | create int
echo "3.14" | create float
echo '"hello"' | create string

# Collections
echo "[]" | create list-empty
echo "[1 2 3]" | create list-simple
echo "{}" | create attrset-empty
echo "{x = 1; y = 2;}" | create attrset-simple

# Functions
echo "x: x" | create lambda
echo "(x: x) 42" | create application

# Let/with
echo "let x = 1; in x" | create let
echo "with {}; 42" | create with

# Control
echo "if true then 1 else 0" | create if
echo "rec { x = 1; y = x; }" | create rec

# Operators
echo "1 + 2" | create add
echo "builtins.add 1 2" | create builtin-add
echo "builtins.map (x: x + 1) [1 2 3]" | create builtin-map

# Copy test files
if [ -d "${TESTS_DIR}" ]; then
    for file in $(find "${TESTS_DIR}" -name "*.nix" -type f -size -2k | head -20); do
        name=$(basename "$file" .nix)
        cp "$file" "${CORPUS_DIR}/${counter}-test-${name}.nix"
        ((counter++))
    done
fi

echo "Corpus complete: ${counter} files in ${CORPUS_DIR}"
