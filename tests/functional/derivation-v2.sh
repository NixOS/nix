#!/usr/bin/env bash

source common.sh

enableFeatures "ca-derivations"

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

# A floating content-addressed version 2 derivation, and an
# input-addressed one downstream of it, whose output paths are therefore
# deferred until it is resolved.

caExpr="
with import ${config_nix};

rec {
  ca = builtins.derivationStrictV2 {
    name = \"derivation-v2-ca\";
    inherit system;
    builder = shell;
    args = [ \"-c\" \"echo ca > \$out\" ];
    env = { out = builtins.placeholder \"out\"; PATH = path; salt = testSalt; };
    outputs.out = { method = \"nar\"; hashAlgo = \"sha256\"; };
  };
  downstream = builtins.derivationStrictV2 {
    name = \"derivation-v2-downstream\";
    inherit system;
    builder = shell;
    args = [ \"-c\" \"cat \${ca.out} > \$out\" ];
    env = { out = builtins.placeholder \"out\"; PATH = path; salt = testSalt; };
    outputs.out = {};
  };
}
"

downstreamDrv=$(nix-instantiate --read-write-mode --eval --raw -E "($caExpr).downstream.drvPath")

# Deferred: no output path is recorded.
grepQuiet -F '[("out","","","",' "$downstreamDrv"

downstreamOut=$(nix-store --realise "$downstreamDrv")
[[ "$(cat "$downstreamOut")" = ca ]]

# The export references graph of a version 2 derivation is JSON, in a
# file of its own.

graphExpr="
with import ${config_nix};

let
  dep = builtins.toFile \"dep\" \"dependency\";
in builtins.derivationStrictV2 {
  name = \"derivation-v2-graph\";
  inherit system;
  builder = shell;
  args = [ \"-c\" \"read -r first < graph; case \\\"\$first\\\" in '['*) ;; *) exit 1;; esac; grep -q \${dep} graph; cp graph \$out\" ];
  env = { out = builtins.placeholder \"out\"; PATH = path; salt = testSalt; };
  outputs.out = {};
  options.exportReferencesGraph.graph = [ dep ];
}
"

graphDrv=$(nix-instantiate --read-write-mode --eval --raw -E "($graphExpr).drvPath")
graphOut=$(nix-store --realise "$graphDrv")
grepQuiet '"narHash"' "$graphOut"
