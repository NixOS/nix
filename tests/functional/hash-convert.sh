#!/usr/bin/env bash

source common.sh

# Conversion with `nix hash` `nix-hash` and `nix hash convert`
try3() {
    # $1 = hash algo
    # $2 = expected hash in base16
    # $3 = expected hash in base32
    # $4 = expected hash in base64
    h64=$(nix hash convert --hash-algo "$1" --to base64 "$2")
    [ "$h64" = "$4" ]
    h64=$(nix-hash --type "$1" --to-base64 "$2")
    [ "$h64" = "$4" ]
    # Deprecated experiment
    h64=$(nix hash to-base64 --type "$1" "$2")
    [ "$h64" = "$4" ]

    sri=$(nix hash convert --hash-algo "$1" --to sri "$2")
    [ "$sri" = "$1-$4" ]
    sri=$(nix-hash --type "$1" --to-sri "$2")
    [ "$sri" = "$1-$4" ]
    sri=$(nix hash to-sri --type "$1" "$2")
    [ "$sri" = "$1-$4" ]
    h32=$(nix hash convert --hash-algo "$1" --to base32 "$2")
    [ "$h32" = "$3" ]
    h32=$(nix-hash --type "$1" --to-base32 "$2")
    [ "$h32" = "$3" ]
    h32=$(nix hash to-base32 --type "$1" "$2")
    [ "$h32" = "$3" ]
    h16=$(nix-hash --type "$1" --to-base16 "$h32")
    [ "$h16" = "$2" ]

    h16=$(nix hash convert --hash-algo "$1" --to base16 "$h64")
    [ "$h16" = "$2" ]
    h16=$(nix hash to-base16 --type "$1" "$h64")
    [ "$h16" = "$2" ]
    h16=$(nix hash convert --to base16 "$sri")
    [ "$h16" = "$2" ]
    h16=$(nix hash to-base16 "$sri")
    [ "$h16" = "$2" ]

    #
    # Converting from SRI
    #

    # Input hash algo auto-detected from SRI and output defaults to SRI as well.
    sri=$(nix hash convert "$1-$4")
    [ "$sri" = "$1-$4" ]

    sri=$(nix hash convert --from sri "$1-$4")
    [ "$sri" = "$1-$4" ]

    sri=$(nix hash convert --to sri "$1-$4")
    [ "$sri" = "$1-$4" ]

    sri=$(nix hash convert --from sri --to sri "$1-$4")
    [ "$sri" = "$1-$4" ]

    sri=$(nix hash convert --to base64 "$1-$4")
    [ "$sri" = "$4" ]

    #
    # Auto-detecting the input from algo and length.
    #

    sri=$(nix hash convert --hash-algo "$1" "$2")
    [ "$sri" = "$1-$4" ]
    sri=$(nix hash convert --hash-algo "$1" "$3")
    [ "$sri" = "$1-$4" ]
    sri=$(nix hash convert --hash-algo "$1" "$4")
    [ "$sri" = "$1-$4" ]

    sri=$(nix hash convert --hash-algo "$1" "$2")
    [ "$sri" = "$1-$4" ]
    sri=$(nix hash convert --hash-algo "$1" "$3")
    [ "$sri" = "$1-$4" ]
    sri=$(nix hash convert --hash-algo "$1" "$4")
    [ "$sri" = "$1-$4" ]

    #
    # Asserting input format succeeds.
    #

    sri=$(nix hash convert --hash-algo "$1" --from base16 "$2")
    [ "$sri" = "$1-$4" ]
    sri=$(nix hash convert --hash-algo "$1" --from nix32 "$3")
    [ "$sri" = "$1-$4" ]
    sri=$(nix hash convert --hash-algo "$1" --from base64 "$4")
    [ "$sri" = "$1-$4" ]

    #
    # Asserting input format fails.
    #

    expectStderr 1 nix hash convert --hash-algo "$1" --from sri "$2" | grepQuiet "is not SRI"
    expectStderr 1 nix hash convert --hash-algo "$1" --from nix32 "$2" | grepQuiet "input hash"
    expectStderr 1 nix hash convert --hash-algo "$1" --from base16 "$3" | grepQuiet "input hash"
    expectStderr 1 nix hash convert --hash-algo "$1" --from nix32 "$4" | grepQuiet "input hash"

    # Base-16 hashes can be in uppercase.
    nix hash convert --hash-algo "$1" --from base16 "$(echo "$2" | tr '[:lower:]' '[:upper:]')"
}

try3 sha1 "800d59cfcd3c05e900cb4e214be48f6b886a08df" "vw46m23bizj4n8afrc0fj19wrp7mj3c0" "gA1Zz808BekAy04hS+SPa4hqCN8="
try3 sha256 "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" "1b8m03r63zqhnjf7l5wnldhh7c134ap5vpj0850ymkq1iyzicy5s" "ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0="
try3 sha512 "204a8fc6dda82f0a0ced7beb8e08a41657c16ef468b228a8279be331a703c33596fd15c13b1b07f9aa1d3bea57789ca031ad85c7a71dd70354ec631238ca3445" "12k9jiq29iyqm03swfsgiw5mlqs173qazm3n7daz43infy12pyrcdf30fkk3qwv4yl2ick8yipc2mqnlh48xsvvxl60lbx8vp38yji0" "IEqPxt2oLwoM7XvrjgikFlfBbvRosiioJ5vjMacDwzWW/RXBOxsH+aodO+pXeJygMa2Fx6cd1wNU7GMSOMo0RQ=="

# Test SRI hashes that lack trailing '=' characters. These are incorrect but we need to support them for backward compatibility.
[[ $(nix hash convert --from sri "sha256-ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0") = sha256-ungWv48Bz+pBQUDeXa4iI7ADYaOWF3qctBD/YfIAFa0= ]]
[[ $(nix hash convert --from sri "sha512-IEqPxt2oLwoM7XvrjgikFlfBbvRosiioJ5vjMacDwzWW/RXBOxsH+aodO+pXeJygMa2Fx6cd1wNU7GMSOMo0RQ") = sha512-IEqPxt2oLwoM7XvrjgikFlfBbvRosiioJ5vjMacDwzWW/RXBOxsH+aodO+pXeJygMa2Fx6cd1wNU7GMSOMo0RQ== ]]
