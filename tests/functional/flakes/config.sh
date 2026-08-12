#!/usr/bin/env bash

source common.sh

cp ../simple.nix ../simple.builder.sh "${config_nix}" "$TEST_HOME"

cd "$TEST_HOME"

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
# To test variations in stderr tty-ness, we run the command in different ways,
# none of which should block on stdin or accept the `nixConfig`s.
nix build < /dev/null
nix build < /dev/null 2>&1 | cat
# EOF counts as no, even when interactive (throw EOF error before)
if type -p script >/dev/null && script -q -c true /dev/null; then
    echo "script is available and GNU-like, so we can ensure a tty"
    script -q -c 'nix build < /dev/null' /dev/null
else
    echo "script is not available or not GNU-like, so we skip testing with an added tty"
fi
# shellcheck disable=SC2235
(! [[ -f post-hook-ran ]])
TODO_NixOS
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

# Credential sources are never accepted from a flake, even when all other
# flake configuration is accepted without prompting.
cat <<EOF > flake.nix
{
    nixConfig.access-tokens = "attacker.example=literal-secret";
    nixConfig.extra-access-tokens = "attacker.example=literal-secret";
    nixConfig.impure-env = "TOKEN=another-literal-secret";
    nixConfig.netrc-file = "/tmp/attacker-netrc";
    nixConfig.secretspec-access-tokens = "attacker.example=GITHUB_TOKEN";
    nixConfig.extra-secretspec-access-tokens = "attacker.example=GITHUB_TOKEN";
    nixConfig.secretspec-file = "/tmp/attacker-secretspec.toml";
    nixConfig.secretspec-provider = "attacker-provider";
    nixConfig.secretspec-profile = "attacker-profile";
    nixConfig.secretspec-scope = "attacker-scope";
    nixConfig.secretspec-impure-env = "TOKEN=GITHUB_TOKEN";
    nixConfig.secretspec-netrc-file = "NIX_NETRC";

    outputs = a: {
       packages.$system.default = import ./simple.nix;
    };
}
EOF

nix build --accept-flake-config --no-link 2> forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'access-tokens' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'extra-access-tokens' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'impure-env' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'netrc-file' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'secretspec-access-tokens' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'extra-secretspec-access-tokens' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'secretspec-file' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'secretspec-provider' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'secretspec-profile' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'secretspec-scope' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'secretspec-impure-env' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuiet "ignoring flake configuration setting 'secretspec-netrc-file' because it is not allowed to be set by flakes" forbidden-settings.err
grepQuietInverse "literal-secret" forbidden-settings.err
