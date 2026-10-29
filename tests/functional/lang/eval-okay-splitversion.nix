map builtins.splitVersion [
  "1"
  # Any .- is skipped regardless of the order between components.
  # Try a bunch of combinations for good measure.
  "1."
  "1.-"
  "1-."
  "...1---"
  "---1..."
  "1.0"
  "1..0"
  "1..-0"
  "1..abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ\t 01234567890\n helloworld ~!pre-.0"
  # `+` and `32` are different components.
  "1.+32.1"
  # `00001` is the same component
  "1-.00001.-.0"
  "1.2.3"
  "1.0"
  "2.1"
  "2.3"
  "2.3.1"
  "2.3a"
  "2.3c"
  "2.3pre1"
  "2.3pre12"
  "2.3pre3"
  "2.3q"
  "2.5"
  "3.1"
]
