#!/usr/bin/env bash

source common.sh

# Meson would split the output into two buffers, ruining the coherence of the log.
exec 1>&2

cat > "$TEST_HOME/expected-machine.json" <<EOF
{"a":{"b":{"c":true}}}
EOF

cat > "$TEST_HOME/expected-pretty.json" <<EOF
{
  "a": {
    "b": {
      "c": true
    }
  }
}
EOF

shellEscapeArray() {
  local result=""
  local separator=""

  for item in "$@"; do
    local escaped
    printf -v escaped "%q" "$item"
    result="${result}${separator}${escaped}"
    separator=" "
  done

  echo "$result"
}

nix eval --json --expr '{ a.b.c = true; }' > "$TEST_HOME/actual.json"
diff -U3 "$TEST_HOME/expected-machine.json" "$TEST_HOME/actual.json"

nix eval --json --pretty --expr \
  '{ a.b.c = true; }' > "$TEST_HOME/actual.json"
diff -U3 "$TEST_HOME/expected-pretty.json" "$TEST_HOME/actual.json"

if type script &>/dev/null; then
  acceptsCommandFlag=0
  # The macOS version just accepts multiple arguments, but util-linux and its `-c` flag only accept a single argument, which is then split on whitespace. We thus have to quote in that case.
  if script -c true /dev/null 2>/dev/null; then
    acceptsCommandFlag=1
  fi

  runScript() {
    if [[ $acceptsCommandFlag -eq 0 ]]; then
      script -e -q /dev/null "$@"
    else
      script -e -q /dev/null -c "$(shellEscapeArray "$@")"
    fi
  }
  runScript nix eval --json --expr "{ a.b.c = true; }" > "$TEST_HOME/actual.json"
  cat "$TEST_HOME/actual.json"
  # script isn't perfectly accurate? Let's grep for a pretty good indication, as the pretty output has a space between the key and the value.
  # diff -U3 "$TEST_HOME/expected-pretty.json" "$TEST_HOME/actual.json"
  grep -F '"a": {' "$TEST_HOME/actual.json"

  runScript nix eval --json --pretty --expr "{ a.b.c = true; }" > "$TEST_HOME/actual.json"
  cat "$TEST_HOME/actual.json"
  grep -F '"a": {' "$TEST_HOME/actual.json"

  runScript nix eval --json --no-pretty --expr "{ a.b.c = true; }" > "$TEST_HOME/actual.json"
  cat "$TEST_HOME/actual.json"
  grep -F '"a":{' "$TEST_HOME/actual.json"

fi
