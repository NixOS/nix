let
  inherit (builtins) mapAttrs;
  reflect = f: {
    args = builtins.functionArgs f;
    strict = builtins.functionStrict f;
    open = builtins.functionOpen f;
    bindAll = builtins.functionBindsAllAttrs f;
  };
  checkCase = k: ({ input, output }:
    let actual = reflect input;
    in
      if actual == output
      then []
      else 
        builtins.trace "\nTest case ${k} failed. Expected:"
        builtins.trace output
        builtins.trace "Actual:"
        builtins.trace actual
          [k]);
  run =
    let fails = builtins.concatLists (builtins.attrValues (mapAttrs checkCase cases));
    in if fails == [ ] then { }
    else throw ("${toString (builtins.length fails)} FAILED\n" + builtins.concatStringsSep "\n" fails);

  cases = {
    plain.input = x: x;
    plain.output = { args = {}; bindAll = null; open = null; strict = null; };
    strict.input = x@{ ... }: x;
    strict.output = { args = {}; bindAll = true; open = true; strict = true; };

    closedEmpty.input = { }: null;
    closedEmpty.output = { args = {}; bindAll = false; open = false; strict = true; };
    closedEmptyBind.input = x@{ }: null;
    closedEmptyBind.output = { args = {}; bindAll = true; open = false; strict = true; };
    openEmpty.input = { ... }: null;
    openEmpty.output = { args = {}; bindAll = false; open = true; strict = true; };
    openEmptyBind.input = x@{ ... }: null;
    openEmptyBind.output = { args = {}; bindAll = true; open = true; strict = true; };
    closed.input = { name, value }: null;
    closed.output = { args = { "name" = false; "value" = false; }; bindAll = false; open = false; strict = true; };
    closedBind.input = x@{ name, value }: null;
    closedBind.output = { args = { "name" = false; "value" = false; }; bindAll = true; open = false; strict = true; };
    open.input = { ... }: null;
    open.output = { args = {}; bindAll = false; open = true; strict = true; };
    openBind.input = x@{ name, value, def ? null, ... }: null;
    openBind.output = {  args = { "name" = false; "value" = false; "def" = true; }; bindAll = true; open = true; strict = true; };

    primop.input = builtins.concatLists;
    primop.output = { args = {}; bindAll = null; open = null; strict = null; };
    primopApp.input = builtins.mapAttrs (k: v: null);
    primopApp.output = { args = {}; bindAll = null; open = null; strict = null; };
  };

in
  run
