source ./common.sh

requireGit

flakeFollowsA=$TEST_ROOT/follows/flakeA
flakeFollowsB=$TEST_ROOT/follows/flakeA/flakeB
flakeFollowsC=$TEST_ROOT/follows/flakeA/flakeB/flakeC
flakeFollowsD=$TEST_ROOT/follows/flakeA/flakeD
flakeFollowsE=$TEST_ROOT/follows/flakeA/flakeE

# Test following path flakerefs.
createGitRepo $flakeFollowsA
mkdir -p $flakeFollowsB
mkdir -p $flakeFollowsC
mkdir -p $flakeFollowsD
mkdir -p $flakeFollowsE

cat > $flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs = {
        B = {
            url = "path:./flakeB";
            inputs.foobar.follows = "foobar";
        };

        foobar.url = "path:$flakeFollowsA/flakeE";
    };
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsB/flake.nix <<EOF
{
    description = "Flake B";
    inputs = {
        foobar.url = "path:$flakeFollowsA/flakeE";
        goodoo.follows = "C/goodoo";
        C = {
            url = "path:./flakeC";
            inputs.foobar.follows = "foobar";
        };
    };
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsC/flake.nix <<EOF
{
    description = "Flake C";
    inputs = {
        foobar.url = "path:$flakeFollowsA/flakeE";
        goodoo.follows = "foobar";
    };
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsD/flake.nix <<EOF
{
    description = "Flake D";
    inputs = {};
    outputs = { ... }: {};
}
EOF

cat > $flakeFollowsE/flake.nix <<EOF
{
    description = "Flake E";
    inputs = {};
    outputs = { ... }: {};
}
EOF

git -C $flakeFollowsA add flake.nix flakeB/flake.nix \
  flakeB/flakeC/flake.nix flakeD/flake.nix flakeE/flake.nix

nix flake metadata $flakeFollowsA

nix flake update --flake $flakeFollowsA

nix flake lock $flakeFollowsA

oldLock="$(cat "$flakeFollowsA/flake.lock")"

# Ensure that locking twice doesn't change anything

nix flake lock $flakeFollowsA

newLock="$(cat "$flakeFollowsA/flake.lock")"

diff <(echo "$newLock") <(echo "$oldLock")

[[ $(jq -c .nodes.B.inputs.C $flakeFollowsA/flake.lock) = '"C"' ]]
[[ $(jq -c .nodes.B.inputs.foobar $flakeFollowsA/flake.lock) = '["foobar"]' ]]
[[ $(jq -c .nodes.C.inputs.foobar $flakeFollowsA/flake.lock) = '["B","foobar"]' ]]

# Ensure removing follows from flake.nix removes them from the lockfile

cat > $flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs = {
        B = {
            url = "path:./flakeB";
        };
        D.url = "path:./flakeD";
    };
    outputs = { ... }: {};
}
EOF

nix flake lock $flakeFollowsA

[[ $(jq -c .nodes.B.inputs.foobar $flakeFollowsA/flake.lock) = '"foobar"' ]]
jq -r -c '.nodes | keys | .[]' $flakeFollowsA/flake.lock | grep "^foobar$"

# Ensure a relative path is not allowed to go outside the store path
cat > $flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs = {
        B.url = "path:../flakeB";
    };
    outputs = { ... }: {};
}
EOF

git -C $flakeFollowsA add flake.nix

expect 1 nix flake lock $flakeFollowsA 2>&1 | grep 'points outside'

# Non-existant follows should print a warning.
cat >$flakeFollowsA/flake.nix <<EOF
{
    description = "Flake A";
    inputs.B = {
        url = "path:./flakeB";
        inputs.invalid.follows = "D";
        inputs.invalid2.url = "path:./flakeD";
    };
    inputs.D.url = "path:./flakeD";
    outputs = { ... }: {};
}
EOF

git -C $flakeFollowsA add flake.nix

nix flake lock "$flakeFollowsA" 2>&1 | grep "warning: input 'B' has an override for a non-existent input 'invalid'"
nix flake lock "$flakeFollowsA" 2>&1 | grep "warning: input 'B' has an override for a non-existent input 'invalid2'"

# Now test follow path overloading
# This tests a lockfile checking regression https://github.com/NixOS/nix/pull/8819
#
# We construct the following graph, where p->q means p has input q.
# A double edge means that the edge gets overridden using `follows`.
#
#      A
#     / \
#    /   \
#   v     v
#   B ==> C   --- follows declared in A
#    \\  /
#     \\/     --- follows declared in B
#      v
#      D
#
# The message was
#    error: input 'B/D' follows a non-existent input 'B/C/D'
#
# Note that for `B` to resolve its follow for `D`, it needs `C/D`, for which it needs to resolve the follow on `C` first.
flakeFollowsOverloadA="$TEST_ROOT/follows/overload/flakeA"
flakeFollowsOverloadB="$TEST_ROOT/follows/overload/flakeA/flakeB"
flakeFollowsOverloadC="$TEST_ROOT/follows/overload/flakeA/flakeB/flakeC"
flakeFollowsOverloadD="$TEST_ROOT/follows/overload/flakeA/flakeB/flakeC/flakeD"

# Test following path flakerefs.
createGitRepo "$flakeFollowsOverloadA"
mkdir -p "$flakeFollowsOverloadB"
mkdir -p "$flakeFollowsOverloadC"
mkdir -p "$flakeFollowsOverloadD"

cat > "$flakeFollowsOverloadD/flake.nix" <<EOF
{
    description = "Flake D";
    inputs = {};
    outputs = { ... }: {};
}
EOF

cat > "$flakeFollowsOverloadC/flake.nix" <<EOF
{
    description = "Flake C";
    inputs.D.url = "path:./flakeD";
    outputs = { ... }: {};
}
EOF

cat > "$flakeFollowsOverloadB/flake.nix" <<EOF
{
    description = "Flake B";
    inputs = {
        C = {
            url = "path:./flakeC";
        };
        D.follows = "C/D";
    };
    outputs = { ... }: {};
}
EOF

# input B/D should be able to be found...
cat > "$flakeFollowsOverloadA/flake.nix" <<EOF
{
    description = "Flake A";
    inputs = {
        B = {
            url = "path:./flakeB";
            inputs.C.follows = "C";
        };
        C.url = "path:./flakeB/flakeC";
    };
    outputs = { ... }: {};
}
EOF

git -C "$flakeFollowsOverloadA" add flake.nix flakeB/flake.nix \
  flakeB/flakeC/flake.nix flakeB/flakeC/flakeD/flake.nix

nix flake metadata "$flakeFollowsOverloadA"
nix flake update --flake "$flakeFollowsOverloadA"
nix flake lock "$flakeFollowsOverloadA"

# Now test follow cycle detection
# We construct the following follows graph:
#
#    foo
#    / ^
#   /   \
#  v     \
# bar -> baz
# The message was
#     error: follow cycle detected: [baz -> foo -> bar -> baz]
flakeFollowCycle="$TEST_ROOT/follows/followCycle"

# Test following path flakerefs.
mkdir -p "$flakeFollowCycle"

cat > $flakeFollowCycle/flake.nix <<EOF
{
    description = "Flake A";
    inputs = {
        foo.follows = "bar";
        bar.follows = "baz";
        baz.follows = "foo";
    };
    outputs = { ... }: {};
}
EOF

checkRes=$(nix flake lock "$flakeFollowCycle" 2>&1 && fail "nix flake lock should have failed." || true)
echo $checkRes | grep -F "error: follow cycle detected: [baz -> foo -> bar -> baz]"


# Test transitive input url locking
# This tests the following lockfile issue: https://github.com/NixOS/nix/issues/9143
#
# We construct the following graph, where p->q means p has input q.
#
# A -> B -> C
#
# And override B/C to flake D, first in A's flake.nix and then with --override-input.
#
# A -> B -> D
flakeFollowsCustomUrlA="$TEST_ROOT/follows/custom-url/flakeA"
flakeFollowsCustomUrlB="$TEST_ROOT/follows/custom-url/flakeA/flakeB"
flakeFollowsCustomUrlC="$TEST_ROOT/follows/custom-url/flakeA/flakeB/flakeC"
flakeFollowsCustomUrlD="$TEST_ROOT/follows/custom-url/flakeA/flakeB/flakeD"


createGitRepo "$flakeFollowsCustomUrlA"
mkdir -p "$flakeFollowsCustomUrlB"
mkdir -p "$flakeFollowsCustomUrlC"
mkdir -p "$flakeFollowsCustomUrlD"

cat > "$flakeFollowsCustomUrlD/flake.nix" <<EOF
{
    description = "Flake D";
    inputs = {};
    outputs = { ... }: {};
}
EOF

cat > "$flakeFollowsCustomUrlC/flake.nix" <<EOF
{
    description = "Flake C";
    inputs = {};
    outputs = { ... }: {};
}
EOF

cat > "$flakeFollowsCustomUrlB/flake.nix" <<EOF
{
    description = "Flake B";
    inputs = {
        C = {
            url = "path:./flakeC";
        };
    };
    outputs = { ... }: {};
}
EOF

cat > "$flakeFollowsCustomUrlA/flake.nix" <<EOF
{
    description = "Flake A";
    inputs = {
        B = {
            url = "path:./flakeB";
            inputs.C.url = "path:./flakeB/flakeD";
        };
    };
    outputs = { ... }: {};
}
EOF

git -C "$flakeFollowsCustomUrlA" add flake.nix flakeB/flake.nix \
  flakeB/flakeC/flake.nix flakeB/flakeD/flake.nix

# lock "original" entry should contain overridden url
json=$(nix flake metadata "$flakeFollowsCustomUrlA" --json)
[[ $(echo "$json" | jq -r .locks.nodes.C.original.path) = './flakeB/flakeD' ]]
rm "$flakeFollowsCustomUrlA"/flake.lock

# if override-input is specified, lock "original" entry should contain original url
json=$(nix flake metadata "$flakeFollowsCustomUrlA" --override-input B/C "path:./flakeB/flakeD" --json)
echo "$json" | jq .locks.nodes.C.original
[[ $(echo "$json" | jq -r .locks.nodes.C.original.path) = './flakeC' ]]
