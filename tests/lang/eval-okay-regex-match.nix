with builtins;

let

  matches = pat: s: match pat s != null;

  splitFN = match "((.*)/)?([^/]*)\\.(nix|cc)";

in

assert  matches "foobar" "foobar";
assert  matches "fo*" "f";
assert !matches "fo+" "f";
assert  matches "fo*" "fo";
assert  matches "fo*" "foo";
assert  matches "fo+" "foo";
assert  matches "fo{1,2}" "foo";
assert !matches "fo{1,2}" "fooo";
assert !matches "fo*" "foobar";
assert  matches "[[:space:]]+([^[:space:]]+)[[:space:]]+" "  foo   ";
assert !matches "[[:space:]]+([[:upper:]]+)[[:space:]]+" "  foo   ";

assert match "(.*)\\.nix" "foobar.nix" == [ "foobar" ];
assert match "[[:space:]]+([[:upper:]]+)[[:space:]]+" "  FOO   " == [ "FOO" ];

assert splitFN "/path/to/foobar.nix" == [ "/path/to/" "/path/to" "foobar" "nix" ];
assert splitFN "foobar.cc" == [ null null "foobar" "cc" ];

true
