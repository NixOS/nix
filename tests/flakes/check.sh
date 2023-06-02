source common.sh

flakeDir=$TEST_ROOT/flake3
mkdir -p $flakeDir

cat > $flakeDir/flake.nix <<EOF
{
  outputs = { self }: {
    overlay = final: prev: {
    };
  };
}
EOF

nix flake check $flakeDir

cat > $flakeDir/flake.nix <<EOF
{
  outputs = { self }: {
    overlay = finalll: prev: {
    };
  };
}
EOF

(! nix flake check $flakeDir)

cat > $flakeDir/flake.nix <<EOF
{
  outputs = { self }: {
    nixosModules.foo = {
      a.b.c = 123;
      foo = true;
    };
  };
}
EOF

nix flake check $flakeDir

cat > $flakeDir/flake.nix <<EOF
{
  outputs = { self }: {
    nixosModules.foo = assert false; {
      a.b.c = 123;
      foo = true;
    };
  };
}
EOF

(! nix flake check $flakeDir)

cat > $flakeDir/flake.nix <<EOF
{
  outputs = { self }: {
    nixosModule = { config, pkgs, ... }: {
      a.b.c = 123;
    };
  };
}
EOF

nix flake check $flakeDir

cat > $flakeDir/flake.nix <<EOF
{
  outputs = { self }: {
    packages.system-1.default = "foo";
    packages.system-2.default = "bar";
  };
}
EOF

nix flake check $flakeDir

checkRes=$(nix flake check --all-systems --keep-going $flakeDir 2>&1 && fail "nix flake check --all-systems should have failed" || true)
echo "$checkRes" | grepQuiet "packages.system-1.default"
echo "$checkRes" | grepQuiet "packages.system-2.default"
