# This is the implementation of the ‘derivation’ builtin function.
# It's actually a wrapper around the ‘derivationStrict’ primop.
# Note that the following comment will be shown in :doc in the repl, but not in the manual.

/**
  Create a derivation.

  # Inputs

  The single argument is an attribute set that describes what to build and how to build it.
  See https://nix.dev/manual/nix/2.23/language/derivations

  # Output

  The result is an attribute set that describes the derivation.
  Notably it contains the outputs, which in the context of the Nix language are special strings that refer to the output paths, which may not yet exist.
  The realisation of these outputs only occurs when needed; for example

    * When `nix-build` or a similar command is run, it realises the outputs that were requested on its command line.
      See https://nix.dev/manual/nix/2.23/command-ref/nix-build

    * When `import`, `readFile`, `readDir` or some other functions are called, they have to realise the outputs they depend on.
      This is referred to as "import from derivation".
      See https://nix.dev/manual/nix/2.23/language/import-from-derivation

  Note that `derivation` is very bare-bones, and provides almost no commands during the build.
  Most likely, you'll want to use functions like `stdenv.mkDerivation` in Nixpkgs to set up a basic environment.
*/
drvAttrs@{
  outputs ? [ "out" ],
  ...
}:

let

  strict = derivationStrict drvAttrs;

  commonAttrs =
    drvAttrs
    // (builtins.listToAttrs outputsList)
    // {
      all = map (x: x.value) outputsList;
      inherit drvAttrs;
    };

  outputToAttrListElement = outputName: {
    name = outputName;
    value = commonAttrs // {
      outPath = builtins.getAttr outputName strict;
      drvPath = strict.drvPath;
      type = "derivation";
      inherit outputName;
    };
  };

  outputsList = map outputToAttrListElement outputs;

in
(builtins.head outputsList).value
