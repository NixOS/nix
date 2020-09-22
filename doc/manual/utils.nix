with builtins;

{
  splitLines = s: filter (x: !isList x) (split "\n" s);

  concatStrings = concatStringsSep "";
}
