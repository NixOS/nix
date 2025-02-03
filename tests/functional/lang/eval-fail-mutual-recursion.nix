# Check that stack frame deduplication only affects consecutive intervals, and
# that they are reported independently of any preceding sections, even if
# they're indistinguishable.
#
# In terms of the current implementation, we check that we clear the set of
# "seen frames" after eliding a group of frames.
#
# Suppose we have:
# - 10 frames in a function A
# - 10 frames in a function B
# - 10 frames in a function A
#
# We want to output:
# - a few frames of A (skip the rest)
# - a few frames of B (skip the rest)
# - a few frames of A (skip the rest)
#
# If we implemented this in the naive manner, we'd instead get:
# - a few frames of A (skip the rest)
# - a few frames of B (skip the rest, _and_ skip the remaining frames of A)
let
  throwAfterB =
    recurse: n:
    if n > 0 then
      throwAfterB recurse (n - 1)
    else if recurse then
      throwAfterA false 10
    else
      throw "Uh oh!";

  throwAfterA =
    recurse: n:
    if n > 0 then
      throwAfterA recurse (n - 1)
    else if recurse then
      throwAfterB true 10
    else
      throw "Uh oh!";
in
throwAfterA true 10
