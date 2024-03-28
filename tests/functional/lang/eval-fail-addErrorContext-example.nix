let
  countDown = n:
    if n == 0
    then throw "kaboom"
    else
      builtins.addErrorContext
        "while counting down; n = ${toString n}"
        ("x" + countDown (n - 1));
in countDown 10
