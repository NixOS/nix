source common.sh

clearStore
rm -rf $TEST_HOME/.cache $TEST_HOME/.config $TEST_HOME/.local

cp ./simple.nix ./simple.builder.sh ./config.nix $TEST_HOME

cd $TEST_HOME

rm -f post-hook-ran
cat <<EOF > echoing-post-hook.sh
#!/bin/sh

echo "ThePostHookRan" > $PWD/post-hook-ran
EOF
chmod +x echoing-post-hook.sh

cat <<EOF > flake.nix
{
    nixConfig.post-build-hook = "$PWD/echoing-post-hook.sh";

    outputs = a: {
       defaultPackage.$system = import ./simple.nix;
    };
}
EOF

# Ugly hack for testing
mkdir -p .local/share/nix
cat <<EOF > .local/share/nix/trusted-settings.json
{"post-build-hook":{"$PWD/echoing-post-hook.sh":true}}
EOF

nix build
test -f post-hook-ran || fail "The post hook should have ran"
