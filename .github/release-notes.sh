#!/usr/bin/env bash

# SC2002 disables "useless cat" warnings.
# I prefer pipelines that start with an explicit input, and go from there.
# Overly fussy.
# shellcheck disable=SC2002

scratch=$(mktemp -d -t tmp.XXXXXXXXXX)
finish() {
  rm -rf "$scratch"
}
trap finish EXIT

DATE=$(date +%Y-%m-%d)
DETERMINATE_NIX_VERSION=$(cat .version-determinate)
TAG_NAME="v${DETERMINATE_NIX_VERSION}"
NIX_VERSION=$(cat .version)
NIX_VERSION_MAJOR_MINOR=$(echo "$NIX_VERSION" | cut -d. -f1,2)
GITHUB_REPOSITORY="${GITHUB_REPOSITORY:-DeterminateSystems/nix-src}"

gh api "/repos/${GITHUB_REPOSITORY}/releases/generate-notes" \
        -f "tag_name=${TAG_NAME}" > "$scratch/notes.json"

trim_trailing_newlines() {
    local text
    text="$(cat)"
    echo -n "${text}"
}

linkify_gh() {
    sed \
        -e 's!\(https://github.com/DeterminateSystems/nix-src/\(pull\|issue\)/\([[:digit:]]\+\)\)![DeterminateSystems/nix-src#\3](\1)!' \
        -e 's#\(https://github.com/DeterminateSystems/nix-src/compare/\([^ ]\+\)\)#[\2](\1)#'
}

(
    cat doc/manual/source/release-notes-determinate/changes.md \
        | sed 's/^.*\(<!-- differences -->\)$/This section lists the differences between upstream Nix '"$NIX_VERSION_MAJOR_MINOR"' and Determinate Nix '"$DETERMINATE_NIX_VERSION"'.\1/' \

    printf "\n<!-- Determinate Nix version %s -->\n" "$DETERMINATE_NIX_VERSION"
    cat "$scratch/notes.json" \
        | jq -r .body \
        | grep -v '^#' \
        | grep -v "Full Changelog" \
        | trim_trailing_newlines \
        | sed -e 's/^\* /\n* /' \
        | linkify_gh
    echo "" # final newline
) > "$scratch/changes.md"

(
    printf "# Release %s (%s)\n\n" \
        "$DETERMINATE_NIX_VERSION" \
        "$DATE"
    printf "* Based on [upstream Nix %s](../release-notes/rl-%s.md).\n\n" \
        "$NIX_VERSION" \
        "$NIX_VERSION_MAJOR_MINOR"

    cat "$scratch/notes.json" | jq -r .body | linkify_gh
) > "$scratch/rl.md"

(
    cat doc/manual/source/SUMMARY.md.in \
        | sed 's/\(<!-- next -->\)$/\1\n  - [Release '"$DETERMINATE_NIX_VERSION"' ('"$DATE"')](release-notes-determinate\/rl-'"$DETERMINATE_NIX_VERSION"'.md)/'
) > "$scratch/summary.md"

mv "$scratch/changes.md" doc/manual/source/release-notes-determinate/changes.md
mv "$scratch/rl.md" "doc/manual/source/release-notes-determinate/rl-${DETERMINATE_NIX_VERSION}.md"
mv "$scratch/summary.md" doc/manual/source/SUMMARY.md.in
