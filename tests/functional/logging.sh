#!/usr/bin/env bash

source common.sh

TODO_NixOS

path=$(nix-build dependencies.nix --no-out-link)

# Test nix-store -l.
[ "$(nix-store -l "$path")" = FOO ]

# Test compressed logs.
clearStore
rm -rf "$NIX_LOG_DIR"
(! nix-store -l "$path")
nix-build dependencies.nix --no-out-link --compress-build-log
[ "$(nix-store -l "$path")" = FOO ]

# test whether empty logs work fine with `nix log`.
builder=$TEST_ROOT/builder
echo -e "#!/bin/sh\nmkdir \$out" > "$builder"
outp="$(nix-build -E \
    'with import '"${config_nix}"'; mkDerivation { name = "fnord"; builder = '"$builder"'; }' \
    --out-link "$TEST_ROOT/result")"

test -d "$outp"

nix log "$outp"

# A cursor forward in build output (as Wine prints instead of a space)
# is shown as the gap it leaves.
cat > "$builder" <<'EOF'
#!/bin/sh
printf 'x\033[1Cy\n'
mkdir $out
EOF
nix-build -E \
    'with import '"${config_nix}"'; mkDerivation { name = "gap"; builder = '"$builder"'; }' \
    --no-out-link 2>&1 | grepQuiet 'x y'

# `nix log` keeps the colors a build printed only on a terminal, or when forced
cat > "$builder" <<'EOF'
#!/bin/sh
printf '\033[31mred\033[0m\tdone\033[1Cnow\n'
mkdir $out
EOF
outp="$(nix-build -E \
    'with import '"${config_nix}"'; mkDerivation { name = "colored"; builder = '"$builder"'; }' \
    --no-out-link)"
[[ "$(nix log "$outp")" = $'red\tdone now' ]]
(( $(CLICOLOR_FORCE=1 nix log "$outp" | grep -c $'\x1b') > 0 ))

if isDaemonNewer "2.26"; then
    # Build works despite ill-formed structured build log entries.
    expectStderr 0 nix build -f ./logging/unusual-logging.nix --no-link | grepQuiet -i 'warning: unable to handle a JSON message from the derivation builder:'
fi

# Test json-log-path.
if [[ "$NIX_REMOTE" != "daemon" ]]; then
    clearStore
    nix build -vv --file dependencies.nix --no-link --json-log-path "$TEST_ROOT/log.json" 2>&1 | grepQuiet 'building.*dependencies-top.drv'
    jq < "$TEST_ROOT/log.json"
    grep '{"action":"start","fields":\[".*-dependencies-top.drv","",1,1\],"id":.*,"level":3,"parent":0' "$TEST_ROOT/log.json" >&2
    (( $(grep -c '{"action":"msg","level":5,"msg":"executing builder .*"}' "$TEST_ROOT/log.json" ) == 5 ))
fi
