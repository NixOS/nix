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

nix eval --json --expr '{ a.b.c = true; }' > "$TEST_HOME/actual.json"
diff -U3 "$TEST_HOME/expected-machine.json" "$TEST_HOME/actual.json"

nix eval --json --pretty --expr \
  '{ a.b.c = true; }' > "$TEST_HOME/actual.json"
diff -U3 "$TEST_HOME/expected-pretty.json" "$TEST_HOME/actual.json"

if type script &>/dev/null; then
  script --return --quiet --command 'nix eval --json --expr "{ a.b.c = true; }"' > "$TEST_HOME/actual.json"
  cat "$TEST_HOME/actual.json"
  # script isn't perfectly accurate? Let's grep for a pretty good indication, as the pretty output has a space between the key and the value.
  # diff -U3 "$TEST_HOME/expected-pretty.json" "$TEST_HOME/actual.json"
  grep -F '"a": {' "$TEST_HOME/actual.json"

  script --return --quiet --command 'nix eval --json --pretty --expr "{ a.b.c = true; }"' > "$TEST_HOME/actual.json"
  cat "$TEST_HOME/actual.json"
  grep -F '"a": {' "$TEST_HOME/actual.json"

  script --return --quiet --command 'nix eval --json --no-pretty --expr "{ a.b.c = true; }"' > "$TEST_HOME/actual.json"
  cat "$TEST_HOME/actual.json"
  grep -F '"a":{' "$TEST_HOME/actual.json"

fi