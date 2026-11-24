#!/usr/bin/env bash

source common.sh

# A version 2 derivation: its strings refer to its own output by
# reference, and an environment variable is passed as a file.

expr="
with import ${config_nix};

builtins.derivationStrictV2 {
  name = \"derivation-v2\";
  inherit system;
  builder = shell;
  args = [ \"-c\" \"echo hello > \$out-tmp; cat \$bigPath >> \$out-tmp; mv \$out-tmp \$out\" ];
  env = {
    out = builtins.placeholder \"out\";
    PATH = path;
    big = { value = \"xyzzy\"; passAsFile = true; };
    salt = testSalt;
  };
  outputs.out = {};
  options.preferLocalBuild = true;
}
"

drvPath=$(nix-instantiate --read-write-mode --eval --raw -E "($expr).drvPath")
staticOutPath=$(nix-instantiate --eval --raw -E "($expr).out")

# The store derivation is in the version 2 form, and records the output
# path computed by static speculation.
grepQuiet '^DrvWithVersion("v2",' "$drvPath"
grepQuiet "\"$staticOutPath\"" "$drvPath"
grepQuiet '("output","out")' "$drvPath"
grepQuiet '"nix:preferLocalBuild","true"' "$drvPath"

outPath=$(nix-store --realise "$drvPath")
[[ "$outPath" = "$staticOutPath" ]]
[[ "$(cat "$outPath")" = "hello
xyzzy" ]]
