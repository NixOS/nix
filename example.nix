let
  showPos =
    pos: if pos == null then "" else "${pos.file}:${toString pos.line}:${toString pos.column}";

  # ({pos,isThunk, printValue} -> a -> b ) -> (a -> c) -> a -> c
  tracePos = builtins.seqWithCallPos (
    {
      pos,
      printValue,
      terminalWidth,
      ...
    }:
    a:
    let
      msg = "${showPos pos} ${printValue a}";
    in
    builtins.seq a builtins.trace msg null
  ) (a: a);
in
  tracePos 42
