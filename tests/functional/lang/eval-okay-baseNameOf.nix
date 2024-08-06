assert baseNameOf "" == "";
assert baseNameOf "." == ".";
assert baseNameOf ".." == "..";
assert baseNameOf "a" == "a";
assert baseNameOf "a." == "a.";
assert baseNameOf "a.." == "a..";
assert baseNameOf "a.b" == "a.b";
assert baseNameOf "a.b." == "a.b.";
assert baseNameOf "a.b.." == "a.b..";
assert baseNameOf "a/" == "a";
assert baseNameOf "a/." == ".";
assert baseNameOf "a/.." == "..";
assert baseNameOf "a/b" == "b";
assert baseNameOf "a/b." == "b.";
assert baseNameOf "a/b.." == "b..";
assert baseNameOf "a/b/c" == "c";
assert baseNameOf "a/b/c." == "c.";
assert baseNameOf "a/b/c.." == "c..";
assert baseNameOf "a/b/c/d" == "d";
assert baseNameOf "a/b/c/d." == "d.";
assert baseNameOf "a\\b" == "a\\b";
assert baseNameOf "C:a" == "C:a";
assert baseNameOf "a//b" == "b";

# It's been like this for close to a decade. We ought to commit to it.
# https://github.com/NixOS/nix/pull/582#issuecomment-121014450
assert baseNameOf "a//" == "";

assert baseNameOf ./foo == "foo";
assert baseNameOf ./foo/bar == "bar";

"ok"
