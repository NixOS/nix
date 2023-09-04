source common.sh

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local

cp ./simple.nix ./simple.builder.sh ./config.nix $TEST_HOME

cd $TEST_HOME

rm -f post-hook-ran
cat <<EOF > echoing-post-hook.sh
#!/bin/sh

echo "ThePostHookRan as \$0" > $PWD/post-hook-ran
EOF
chmod +x echoing-post-hook.sh

cat <<EOF > flake.nix
{
    nixConfig.post-build-hook = ./echoing-post-hook.sh;
    nixConfig.allow-dirty = false; # See #5621

    outputs = a: {
       packages.$system.default = import ./simple.nix;
    };
}
EOF

# Without --accept-flake-config, the post hook should not run.
nix build < /dev/null
(! [[ -f post-hook-ran ]])
clearStore

nix build --accept-flake-config
test -f post-hook-ran || fail "The post hook should have ran"

# Make sure that the path to the post hook doesn’t change if we change
# something in the flake.
# Otherwise the user would have to re-validate the setting each time.
mv post-hook-ran previous-post-hook-run
echo "# Dummy comment" >> flake.nix
clearStore
nix build --accept-flake-config
diff -q post-hook-ran previous-post-hook-run || \
    fail "Both post hook runs should report the same filename"
