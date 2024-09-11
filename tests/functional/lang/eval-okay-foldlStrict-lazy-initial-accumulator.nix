# Checks that the nul value for the accumulator is not forced unconditionally.
# Some languages provide a foldl' that is strict in this argument, but Nix does not.
builtins.foldl'
  (_: x: x)
  (throw "This is never forced")
  [ "but the results of applying op are" 42 ]
