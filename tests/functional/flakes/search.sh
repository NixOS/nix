#!/usr/bin/env bash

source common.sh

createFlake1

cat > "$flake1Dir/flake.nix" <<EOF
{
  description = "Bla bla";

  outputs = { self }: {

    packages.$system.hello = derivation {
      name = "hello";
      meta.description = "An implementation of the Hello World program";
    };

    packages.$system.foo = derivation {
      name = "foo";
      meta.description = "A foo package";
    };

    packages.other-system.bar = derivation {
      name = "bar";
      meta.description = "A bar package";
    };

    legacyPackages.$system.xyzzy = derivation {
      name = "xyzzy";
      meta.description = "A xyzzy package";
    };

    otherSchema.$system.aap = derivation {
      name = "aap";
      meta.description = "An aap package";
    };

  };
}
EOF

expectStderr 0 nix search flake1 ^ | grepQuiet "Found 3 matching packages out of 3."
expectStderr 0 nix search flake1 ^ | grepQuiet "legacyPackages.$system.xyzzy"
expectStderr 0 nix search flake1#foo ^ | grepQuiet "Found 1 matching packages out of 1."
expectStderr 1 nix search flake1#bar ^
expectStderr 0 nix search flake1#packages.other-system ^ | grepQuiet "packages.other-system.bar"
expectStderr 0 nix search "flake1#otherSchema.$system" ^ | grepQuiet "otherSchema.$system.aap"
