# Test that derivations with __structuredAttrs and deeply nested structures
# produce a controlled stack overflow error rather than a segfault.

derivation {
  name = "test";
  system = "x86_64-linux";
  builder = "/bin/sh";
  __structuredAttrs = true;
  nested =
    let
      long = builtins.genList (x: x) 100000;
      reverseLinkedList = builtins.foldl' (tail: head: { inherit head tail; }) null long;
    in
    reverseLinkedList;
}
