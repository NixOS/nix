# shellcheck shell=bash

# Remove ANSI escape sequences. They can prevent grep from finding a match.
stripColors () {
    sed -E 's/\x1B\[([0-9]{1,2}(;[0-9]{1,2})?)?[m|K]//g'
}

testReplResponseGeneral () {
    local grepMode commands expectedResponse response
    grepMode="$1"; shift
    commands="$1"; shift
    # Expected response can contain newlines.
    # grep can't handle multiline patterns, so replace newlines with TEST_NEWLINE
    # in both expectedResponse and response.
    # awk ORS always adds a trailing record separator, so we strip it with sed.
    expectedResponse="$(printf '%s' "$1" | awk 1 ORS=TEST_NEWLINE | sed 's/TEST_NEWLINE$//')"; shift
    # We don't need to strip trailing record separator here, since extra data is ok.
    response="$(nix repl "$@" <<< "$commands" 2>&1 | stripColors | awk 1 ORS=TEST_NEWLINE)"
    printf '%s' "$response" | grepQuiet "$grepMode" -s "$expectedResponse" \
      || fail "$(echo "repl command set:

$commands

does not respond with:

---
$expectedResponse
---

but with:

---
$response
---

" | sed 's/TEST_NEWLINE/\n/g')"
}

testReplResponse () {
    testReplResponseGeneral --basic-regexp "$@"
}

testReplResponseNoRegex () {
    testReplResponseGeneral --fixed-strings "$@"
}

nixVersion="$(nix eval --impure --raw --expr 'builtins.nixVersion' --extra-experimental-features nix-command)"

# TODO: write a repl interacter for testing. Papering over the differences between readline / editline and between platforms is a pain.

# I couldn't get readline and editline to agree on the newline before the prompt,
# so let's just force it to be one empty line.
stripEmptyLinesBeforePrompt() {
  # --null-data:  treat input as NUL-terminated instead of newline-terminated
  sed --null-data 's/\n\n*nix-repl>/\n\nnix-repl>/g'
}

# We don't get a final prompt on darwin, so we strip this as well.
stripFinalPrompt() {
  # Strip the final prompt and/or any trailing spaces
  sed --null-data \
    -e 's/\(.*[^\n]\)\n\n*nix-repl>[ \n]*$/\1/' \
    -e 's/[ \n]*$/\n/'
}

filterReplOutput () {
  # That is right, we are also filtering out the testdir _without underscores_.
  # This is crazy, but without it, GHA will fail to run the tests, showing paths
  # _with_ underscores in the set -x log, but _without_ underscores in the
  # supposed nix repl output. I have looked in a number of places, but I cannot
  # find a mechanism that could cause this to happen.
  local testDirNoUnderscores
  local testDir="$1"

  testDirNoUnderscores="${testDir//_/}"

  stripColors \
    | tr -d '\0' \
    | stripEmptyLinesBeforePrompt \
    | stripFinalPrompt \
    | sed \
      -e "s@$testDir@/path/to/repl/suite@g" \
      -e "s@$testDirNoUnderscores@/path/to/repl/suite@g" \
      -e "s@$nixVersion@<nix version>@g" \
    | grep -vF $'warning: you don\'t have Internet access; disabling some network-dependent features' \
    ;
}

runRepl () {
    _NIX_TEST_RAW_MARKDOWN=1 \
    _NIX_TEST_REPL_ECHO=1 \
    nix repl "${@:2}" 2>&1 | filterReplOutput "$1"
}

runDebugRepl () {
    _NIX_TEST_RAW_MARKDOWN=1 \
    _NIX_TEST_REPL_ECHO=1 \
    nix eval --file "$1" --debugger "${@:3}" 2>&1 | filterReplOutput "$2"
}
