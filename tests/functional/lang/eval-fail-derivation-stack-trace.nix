# The "while evaluating derivation %s" messages should always appear in the
# stack trace, even when the trace is truncated, so that users can see how to
# remove problem packages from their configurations.

let
  countDown =
    i:
    if i == 0 then
      throw "kaboom"
    else
      derivation {
        name = "test-${toString i}";
        system = "x86_64-linux";
        builder = "/bin/sh";
        buildInputs = [ (countDown (i - 1)) ];
      };
in
countDown 5
