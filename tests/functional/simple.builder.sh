#!/usr/bin/env bash

echo "PATH=$PATH"

# Verify that the PATH is empty.
if mkdir foo 2> /dev/null; then exit 1; fi

# Set a PATH (!!! impure).
# shellcheck disable=SC2154
export PATH=$goodPath
# shellcheck disable=SC2154
mkdir "$out"

echo "Hello World!" > "$out"/hello
