source ./common.sh

requireGit

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config

flakeA=$TEST_ROOT/flakeA
flakeB=$TEST_ROOT/flakeB
flakeC=$TEST_ROOT/flakeC
flakeC2=$TEST_ROOT/flakeC2
flakeD=$TEST_ROOT/flakeD
flakeD2=$TEST_ROOT/flakeD2
flakeD3=$TEST_ROOT/flakeD3
flakeE=$TEST_ROOT/flakeE
flakeE2=$TEST_ROOT/flakeE2

for repo in "$flakeA" "$flakeB" "$flakeC" "$flakeC2" "$flakeD" "$flakeD2" "$flakeD3" "$flakeE" "$flakeE2"; do
    createGitRepo "$repo"
done

# Test simple override
# A(override B.C=C2) -> B -> C
cat > "$flakeA/flake.nix" <<EOF
{
  inputs.b.url = git+file://$flakeB;
  inputs.b.inputs.c.url = git+file://$flakeC2;
  outputs = { self, b }: {
    foo = "A " + b.foo;
  };
}
EOF
git -C "$flakeA" add flake.nix
git -C "$flakeA" commit -a -m 'initial'

cat > "$flakeB/flake.nix" <<EOF
{
  inputs.c.url = git+file://$flakeC;
  outputs = { self, c }: {
    foo = "B " + c.foo;
  };
}
EOF
git -C "$flakeB" add flake.nix
git -C "$flakeB" commit -a -m 'initial'

cat > "$flakeC/flake.nix" <<EOF
{
  outputs = { self }: {
    foo = "C";
  };
}
EOF
git -C "$flakeC" add flake.nix
git -C "$flakeC" commit -a -m 'initial'

cat > "$flakeC2/flake.nix" <<EOF
{
  outputs = { self }: {
    foo = "C2";
  };
}
EOF
git -C "$flakeC2" add flake.nix
git -C "$flakeC2" commit -a -m 'initial'

[[ $(nix eval --raw "$flakeB#foo") = "B C" ]]
[[ $(nix eval --raw "$flakeA#foo") = "A B C2" ]]

# Test simple override from command line
# A -> B -> C
# --input-override a.b.c = C2
cat > "$flakeA/flake.nix" <<EOF
{
  inputs.b.url = git+file://$flakeB;
  outputs = { self, b }: {
    foo = "A " + b.foo;
  };
}
EOF
git -C "$flakeA" add flake.nix
git -C "$flakeA" commit -a -m 'initial'

# B, C, and C2 same as previous test
[[ $(nix eval --raw "$flakeA#foo" --recreate-lock-file) = "A B C" ]]
[[ $(nix eval --raw "$flakeA#foo" --override-input b/c git+file://$flakeC2) = "A B C2" ]]

# Test unused overrides detected
# A(override B.X=A, B.X.X=B, B.Y=C, B.C.Z=D) -> B -> C
cat > "$flakeA/flake.nix" <<EOF
{
  inputs.b.url = git+file://$flakeB;
  inputs.b.inputs.x.url = git+file://$flakeA;
  inputs.b.inputs.x.inputs.x.url = git+file://$flakeB;
  inputs.b.inputs.y.url = git+file://$flakeC;
  inputs.b.inputs.c.inputs.z.url = git+file://$flakeD;
  outputs = { self, b }: {
    foo = "A " + b.foo;
  };
}
EOF
git -C "$flakeA" add flake.nix
git -C "$flakeA" commit -a -m 'initial'

# B and C same as previous test
[[ $(nix eval --raw "$flakeA#foo") = "A B C" ]]
expectedOutputRegex="warning: input 'b/c' has an override for a non-existent input 'z'
warning: input 'b' has an override for a non-existent input 'x'
warning: input 'b/x' has an override for a non-existent input 'x'
warning: input 'b' has an override for a non-existent input 'y'"
[[ $(nix eval "$flakeA#foo" 2>&1 | grep "has an override") =~ $expectedOutputRegex ]]

# Test multi-level overrides
# A(override B.C.D=D2) -> B -> C -> D

cat > "$flakeA/flake.nix" <<EOF
{
  inputs.b.url = git+file://$flakeB;
  inputs.b.inputs.c.inputs.d.url = git+file://$flakeD2;
  outputs = { self, b }: {
    foo = "A " + b.foo;
  };
}
EOF
git -C "$flakeA" add flake.nix
git -C "$flakeA" commit -a -m 'initial'

cat > "$flakeB/flake.nix" <<EOF
{
  inputs.c.url = git+file://$flakeC;
  outputs = { self, c }: {
    foo = "B " + c.foo;
  };
}
EOF
git -C "$flakeB" add flake.nix
git -C "$flakeB" commit -a -m 'initial'

cat > "$flakeC/flake.nix" <<EOF
{
  inputs.d.url = git+file://$flakeD;
  outputs = { self, d }: {
    foo = "C " + d.foo;
  };
}
EOF
git -C "$flakeC" add flake.nix
git -C "$flakeC" commit -a -m 'initial'

cat > "$flakeD/flake.nix" <<EOF
{
  outputs = { self }: {
    foo = "D";
  };
}
EOF
git -C "$flakeD" add flake.nix
git -C "$flakeD" commit -a -m 'initial'

cat > "$flakeD2/flake.nix" <<EOF
{
  outputs = { self }: {
    foo = "D2";
  };
}
EOF
git -C "$flakeD2" add flake.nix
git -C "$flakeD2" commit -a -m 'initial'

[[ $(nix eval --raw "$flakeB#foo" --recreate-lock-file) = "B C D" ]]
[[ $(nix eval --raw "$flakeA#foo" --recreate-lock-file) = "A B C D2" ]]

# Test overriding an override
# A(override B.C.D=D3) -> B (override C.D=D2)-> C -> D
cat > "$flakeA/flake.nix" <<EOF
{
  inputs.b.url = git+file://$flakeB;
  inputs.b.inputs.c.inputs.d.url = git+file://$flakeD3;
  outputs = { self, b }: {
    foo = "A " + b.foo;
  };
}
EOF
git -C "$flakeA" add flake.nix
git -C "$flakeA" commit -a -m 'initial'

cat > "$flakeB/flake.nix" <<EOF
{
  inputs.c.url = git+file://$flakeC;
  inputs.c.inputs.d.url = git+file://$flakeD2;
  outputs = { self, c }: {
    foo = "B " + c.foo;
  };
}
EOF
git -C "$flakeB" add flake.nix
git -C "$flakeB" commit -a -m 'initial'

cat > "$flakeD3/flake.nix" <<EOF
{
  outputs = { self }: {
    foo = "D3";
  };
}
EOF
git -C "$flakeD3" add flake.nix
git -C "$flakeD3" commit -a -m 'initial'

# C, D, and D2 same as previous test

[[ $(nix eval --raw "$flakeC#foo") = "C D" ]]
[[ $(nix eval --raw "$flakeB#foo") = "B C D2" ]]
[[ $(nix eval --raw "$flakeA#foo") = "A B C D3" ]]

# Test overrides are merged
# A(override B.C.D.E=E2) -> B (override C.D=D2)-> C -> D -> E

cat > "$flakeA/flake.nix" <<EOF
{
  inputs.b.url = git+file://$flakeB;
  inputs.b.inputs.c.inputs.d.inputs.e.url = git+file://$flakeE2;
  outputs = { self, b }: {
    foo = "A " + b.foo;
  };
}
EOF
git -C "$flakeA" add flake.nix
git -C "$flakeA" commit -a -m 'initial'

cat > "$flakeB/flake.nix" <<EOF
{
  inputs.c.url = git+file://$flakeC;
  inputs.c.inputs.d.url = git+file://$flakeD2;
  outputs = { self, c }: {
    foo = "B " + c.foo;
  };
}
EOF
git -C "$flakeB" add flake.nix
git -C "$flakeB" commit -a -m 'initial'

cat > "$flakeC/flake.nix" <<EOF
{
  inputs.d.url = git+file://$flakeD;
  outputs = { self, d }: {
    foo = "C " + d.foo;
  };
}
EOF
git -C "$flakeC" add flake.nix
git -C "$flakeC" commit -a -m 'initial'

cat > "$flakeD/flake.nix" <<EOF
{
  inputs.e.url = git+file://$flakeE;
  outputs = { self, e }: {
    foo = "D " + e.foo;
  };
}
EOF
git -C "$flakeD" add flake.nix
git -C "$flakeD" commit -a -m 'initial'

cat > "$flakeD2/flake.nix" <<EOF
{
  inputs.e.url = git+file://$flakeE;
  outputs = { self, e }: {
    foo = "D2 " + e.foo;
  };
}
EOF
git -C "$flakeD2" add flake.nix
git -C "$flakeD2" commit -a -m 'initial'


cat > "$flakeE/flake.nix" <<EOF
{
  outputs = { self }: {
    foo = "E";
  };
}
EOF
git -C "$flakeE" add flake.nix
git -C "$flakeE" commit -a -m 'initial'

cat > "$flakeE2/flake.nix" <<EOF
{
  outputs = { self }: {
    foo = "E2";
  };
}
EOF
git -C "$flakeE2" add flake.nix
git -C "$flakeE2" commit -a -m 'initial'

[[ $(nix eval --raw "$flakeC#foo" --recreate-lock-file) = "C D E" ]]
[[ $(nix eval --raw "$flakeB#foo") = "B C D2 E" ]]
[[ $(nix eval --raw "$flakeA#foo" --recreate-lock-file) = "A B C D2 E2" ]]

# Test overrides set by an override are used
# A(override B.C=C2) -> B -> C -> D -> E
# C2 (override D.E=E2)
# E2 should be used

cat > "$flakeA/flake.nix" <<EOF
{
  inputs.b.url = git+file://$flakeB;
  inputs.b.inputs.c.url = git+file://$flakeC2;
  outputs = { self, b }: {
    foo = "A " + b.foo;
  };
}
EOF
git -C "$flakeA" add flake.nix
git -C "$flakeA" commit -a -m 'initial'

cat > "$flakeB/flake.nix" <<EOF
{
  inputs.c.url = git+file://$flakeC;
  outputs = { self, c }: {
    foo = "B " + c.foo;
  };
}
EOF
git -C "$flakeB" add flake.nix
git -C "$flakeB" commit -a -m 'initial'

cat > "$flakeC/flake.nix" <<EOF
{
  inputs.d.url = git+file://$flakeD;
  outputs = { self, d }: {
    foo = "C " + d.foo;
  };
}
EOF
git -C "$flakeC" add flake.nix
git -C "$flakeC" commit -a -m 'initial'

cat > "$flakeC2/flake.nix" <<EOF
{
  inputs.d.url = git+file://$flakeD;
  inputs.d.inputs.e.url = git+file://$flakeE2;
  outputs = { self, d }: {
    foo = "C2 " + d.foo;
  };
}
EOF
git -C "$flakeC2" add flake.nix
git -C "$flakeC2" commit -a -m 'initial'

# D, E, and E2 same as previous test

# TODO is there a bug around when to update inputs?
# The tests fail without removing these locks.
rm -f {$flakeA,$flakeB}/flake.lock
[[ $(nix eval --raw "$flakeC#foo") = "C D E" ]]
[[ $(nix eval --raw "$flakeA#foo") = "A B C2 D E2" ]]

# Test overrides of overridden inputs get discarded
# A(override B.C=C2) -> B -> C(override D.E=E2) -> D -> E
# E should not be overridden since the input setting that override (C) was
# overridden with an input (C2) that did not have an override for E.

# A and B same

cat > "$flakeC/flake.nix" <<EOF
{
  inputs.d.url = git+file://$flakeD;
  inputs.d.inputs.e.url = git+file://$flakeE2;
  outputs = { self, d }: {
    foo = "C " + d.foo;
  };
}
EOF
git -C "$flakeC" add flake.nix
git -C "$flakeC" commit -a -m 'initial'

cat > "$flakeC2/flake.nix" <<EOF
{
  inputs.d.url = git+file://$flakeD;
  outputs = { self, d }: {
    foo = "C2 " + d.foo;
  };
}
EOF
git -C "$flakeC2" add flake.nix
git -C "$flakeC2" commit -a -m 'initial'

# D, E, and E2 same as previous test

[[ $(nix eval --raw "$flakeC#foo") = "C D E2" ]]
[[ $(nix eval --raw "$flakeA#foo" --recreate-lock-file) = "A B C2 D E" ]]
