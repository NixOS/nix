# Run:
# GC_INITIAL_HEAP_SIZE=$[1024 * 1024] NIX_SHOW_STATS=1 nix eval -f gc-coroutine-test.nix  -vvvv

let
  inherit (builtins)
    foldl'
    isList
    ;

  # Generate a tree of numbers, n deep, such that the numbers add up to (1 + salt) * 10^n.
  # The salting makes the numbers all different, increasing the likelihood of catching
  # any memory corruptions that might be caused by the GC or otherwise.
  garbage =
    salt: n:
    if n == 0 then
      [ (1 + salt) ]
    else
      [
        (garbage (10 * salt + 1) (n - 1))
        (garbage (10 * salt - 1) (n - 1))
        (garbage (10 * salt + 2) (n - 1))
        (garbage (10 * salt - 2) (n - 1))
        (garbage (10 * salt + 3) (n - 1))
        (garbage (10 * salt - 3) (n - 1))
        (garbage (10 * salt + 4) (n - 1))
        (garbage (10 * salt - 4) (n - 1))
        (garbage (10 * salt + 5) (n - 1))
        (garbage (10 * salt - 5) (n - 1))
      ];

  pow = base: n: if n == 0 then 1 else base * (pow base (n - 1));

  sumNestedLists = l: if isList l then foldl' (a: b: a + sumNestedLists b) 0 l else l;

in
assert sumNestedLists (garbage 0 3) == pow 10 3;
assert sumNestedLists (garbage 0 6) == pow 10 6;
builtins.foldl'
  (
    a: b:
    assert
      "${builtins.path {
        path = ./src;
        filter =
          path: type:
          # We're not doing common subexpression elimination, so this reallocates
          # the fairly big tree over and over, producing a lot of garbage during
          # source filtering, whose filter runs in a coroutine.
          assert sumNestedLists (garbage 0 3) == pow 10 3;
          true;
      }}" == "${./src}";

    # These asserts don't seem necessary, as the lambda value get corrupted first
    assert a.okay;
    assert b.okay;
    {
      okay = true;
    }
  )
  { okay = true; }
  [
    { okay = true; }
    { okay = true; }
    { okay = true; }
  ]
