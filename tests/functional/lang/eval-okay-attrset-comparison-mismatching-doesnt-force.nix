# Test that comparing attribute sets with mismatched attrNames never forces values.

let
  attrs1 = {
    # This symbol will appear first in the symbol table, but later in the canonical
    # lexicographical order.
    symbol2 = throw "a";
    symbol3 = throw "b";
  };

  attrs2 = {
    symbol2 = throw "a";
    symbol4 = throw "b";
  };

  equal = attrs1 == attrs2;
in
assert attrs1 != attrs2;
assert !equal;
"ok"
