# Test that toJSON on a deeply nested structure produces a controlled
# stack overflow error rather than a segfault.

let
  long = builtins.genList (x: x) 100000;
  reverseLinkedList = builtins.foldl' (tail: head: { inherit head tail; }) null long;
in
builtins.toJSON reverseLinkedList
