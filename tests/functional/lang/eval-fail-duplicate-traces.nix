# Check that we only omit duplicate stack traces when there's a bunch of them.
# Here, there's only a couple duplicate entries, so we output them all.
let
  throwAfter = n: if n > 0 then throwAfter (n - 1) else throw "Uh oh!";
in
throwAfter 2
