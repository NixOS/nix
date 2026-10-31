let
  drvPath = "/nix/store/arhvjaf6zmlyn8vh8fgn55rpwnxq0n7l-a.drv";
  withContext = context: builtins.appendContext "" { ${drvPath} = context; };

  nested = withContext {
    outputs = [
      [
        "out"
        "dev"
      ]
      [
        "out"
        "lib"
        "bin"
      ]
    ];
  };
  withOutput = withContext {
    outputs = [
      "out"
      [
        "out"
        "lib"
      ]
    ];
  };
  withDrvDeep = withContext {
    allOutputs = true;
    outputs = [
      [
        "out"
        "lib"
      ]
    ];
  };

  roundTrip =
    s: builtins.appendContext (builtins.unsafeDiscardStringContext s) (builtins.getContext s);
in
{
  nested = builtins.getContext nested;
  withOutput = builtins.getContext withOutput;
  withDrvDeep = builtins.getContext withDrvDeep;

  # String `==` ignores the context, so compare the contexts.
  roundTrip = map (s: builtins.getContext (roundTrip s) == builtins.getContext s) [
    nested
    withOutput
    withDrvDeep
  ];

  oneNameList = builtins.getContext (withContext {
    outputs = [ [ "out" ] ];
  });

  # Chains that differ in length and name, where lexicographic order and
  # shortest-first order disagree.
  sorted = builtins.getContext (withContext {
    outputs = [
      "out"
      [
        "out"
        "lib"
      ]
      "b"
      [
        "a"
        "z"
        "y"
      ]
      [
        "a"
        "z"
      ]
    ];
  });
}
