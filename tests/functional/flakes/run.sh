#!/usr/bin/env bash

source ../common.sh

TODO_NixOS

clearStore
rm -rf "$TEST_HOME"/.cache "$TEST_HOME"/.config "$TEST_HOME"/.local

cp ../shell-hello.nix "${config_nix}" "$TEST_HOME"
cd "$TEST_HOME"

cat <<EOF > flake.nix
{
    outputs = {self}: {
      packages.$system.pkgAsPkg = (import ./shell-hello.nix).hello;
      packages.$system.appAsApp = self.packages.$system.appAsApp;

      apps.$system.pkgAsApp = self.packages.$system.pkgAsPkg;
      apps.$system.appAsApp = {
        type = "app";
        program = "\${(import ./shell-hello.nix).hello}/bin/hello";
      };
    };
}
EOF
nix run --no-write-lock-file .#appAsApp
nix run --no-write-lock-file .#pkgAsPkg

! nix run --no-write-lock-file .#pkgAsApp || fail "'nix run' shouldn’t accept an 'app' defined under 'packages'"
! nix run --no-write-lock-file .#appAsPkg || fail "elements of 'apps' should be of type 'app'"

# Test that we're not setting any more environment variables than necessary.
# For instance, we might set an environment variable temporarily to affect some
# initialization or whatnot, but this must not leak into the environment of the
# command being run.
env > "$TEST_ROOT"/expected-env
nix run -f shell-hello.nix env > "$TEST_ROOT"/actual-env
# Remove/reset variables we expect to be different.
# - PATH is modified by nix shell
# - we unset TMPDIR on macOS if it contains /var/folders. bad. https://github.com/NixOS/nix/issues/7731
# - _ is set by bash and is expected to differ because it contains the original command
# - __CF_USER_TEXT_ENCODING is set by macOS and is beyond our control
# - __LLVM_PROFILE_RT_INIT_ONCE - implementation detail of LLVM source code coverage collection
sed -i \
  -e 's/PATH=.*/PATH=.../' \
  -e 's/_=.*/_=.../' \
  -e '/^TMPDIR=\/var\/folders\/.*/d' \
  -e '/^__CF_USER_TEXT_ENCODING=.*$/d' \
  -e '/^__LLVM_PROFILE_RT_INIT_ONCE=.*$/d' \
  "$TEST_ROOT"/expected-env "$TEST_ROOT"/actual-env
sort "$TEST_ROOT"/expected-env | uniq > "$TEST_ROOT"/expected-env.sorted
# nix run appears to clear _. I don't understand why. Is this ok?
echo "_=..." >> "$TEST_ROOT"/actual-env
sort "$TEST_ROOT"/actual-env | uniq > "$TEST_ROOT"/actual-env.sorted
diff "$TEST_ROOT"/expected-env.sorted "$TEST_ROOT"/actual-env.sorted

clearStore

