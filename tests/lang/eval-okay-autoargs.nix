let

  foobar = "foobar";

in

{ xyzzy2 ? xyzzy # mutually recursive args
, xyzzy ? "blaat" # will be overridden by --argstr
, fb ? foobar
, lib # will be set by --arg
, ...
}@args: # args.abc will be set by --arg too

{
  result = lib.concat [xyzzy xyzzy2 fb args.abc];
}
