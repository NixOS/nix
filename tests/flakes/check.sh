source common.sh

flakeDir=$TEST_ROOT/flake3
depDir=$TEST_ROOT/flakedep
mkdir -p $flakeDir $depDir

cat > $depDir/flake.nix <<EOF
{
  outputs = { ... }: {
  };
}
EOF

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
  outputs = { self, ... }: {
    overlays.x86_64-linux.foo = final: prev: {
    };
  };
}
EOF

checkRes=$(nix flake check $flakeDir 2>&1 && fail "nix flake check --all-systems should have failed" || true)
echo "$checkRes" | grepQuiet "error: overlay is not a function, but a set instead"

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

cat > $flakeDir/flake.nix <<EOF
{
  inputs = {
    dep.url = "$depDir";
  };
  outputs = { self, dep }: {
  };
}
EOF

nix flake check $flakeDir

cat > $flakeDir/flake.nix <<EOF
{
  inputs = {
    self.url = "$depDir";
  };
  outputs = { self }: {
  };
}
EOF

expectStderr 1 nix flake check $flakeDir | grep -F "flake input name 'self' is reserved"


cat > $flakeDir/flake.nix <<EOF
{
  inputs = {
    meta.url = "$depDir";
  };
  outputs = args@{ self, ... }: {
  };
}
EOF

expectStderr 1 nix flake check $flakeDir | grep -F "flake input name 'meta' is reserved"
