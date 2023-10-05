# Tests that the result of applying op is forced even if the value is never used
builtins.foldl'
  (_: f: f null)
  null
  [ (_: throw "Not the final value, but is still forced!") (_: 23) ]
