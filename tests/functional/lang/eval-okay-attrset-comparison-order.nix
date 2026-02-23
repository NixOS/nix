# Test that comparison happens in lexicographical order of attribute names and
# not in the unspecified symbol table order. Symbol table ordering is an impurity
# that changes depending on the evaluation order.

let
  attrs1 = {
    # This symbol will appear first in the symbol table, but later in the canonical
    # lexicographical order.
    symbol2 = throw "bad";
    symbol1 = "str1";
  };

  attrs2 = {
    symbol1 = "str2";
    symbol2 = "ok";
  };

  equal = attrs1 == attrs2;
in
assert attrs1 != attrs2;
assert !equal;
"ok"
