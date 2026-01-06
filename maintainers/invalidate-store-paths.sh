#!/usr/bin/env bash
set -euo pipefail
set -x

git ls-files -z \
    | xargs -0 grep -o '[0123456789abcdfghijklmnpqrsvwxyz]\{32\}' 2> /dev/null \
    | rev \
    | cut -d: -f1 \
    | rev \
    | sort \
    | uniq \
    | while read -r oldhash; do
        if ! curl --fail -I "https://cache.nixos.org/$oldhash.narinfo" > /dev/null 2>&1; then
            continue
        fi

        newhash=$(
            nix eval --expr "builtins.toFile \"006c6ssvddri1sg34wnw65mzd05pcp3qliylxlhv49binldajba5\" \"$oldhash\"" \
                | cut -d- -f1 \
                | cut -d/ -f4
        )

        msg=$(printf "bad: %s -> %s" "$oldhash" "$newhash")
        echo "$msg"
        git ls-files -z \
            | xargs -0 grep -a -l "$oldhash" 2> /dev/null \
            | while read -r file; do
                [ -L "$file" ] && continue
                perl -pi -e "s/$oldhash/$newhash/g" "$file" || true
            done || true
        git commit -am "$msg"
    done
