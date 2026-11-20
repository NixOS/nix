with import ./config.nix;

rec {

  dep = import ./dependencies.nix { };

  makeTest =
    nr: args:
    mkDerivation (
      {
        name = "check-refs-" + toString nr;
      }
      // args
    );

  src = builtins.toFile "aux-ref" "bla bla";

  test1 = makeTest 1 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $dep $out/link";
    inherit dep;
  };

  test2 = makeTest 2 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s ${src} $out/link";
    inherit dep;
  };

  test3 = makeTest 3 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $dep $out/link";
    allowedReferences = [ ];
    inherit dep;
  };

  test4 = makeTest 4 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $dep $out/link";
    allowedReferences = [ dep ];
    inherit dep;
  };

  test5 = makeTest 5 {
    builder = builtins.toFile "builder.sh" "mkdir $out";
    allowedReferences = [ ];
    inherit dep;
  };

  test6 = makeTest 6 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $out $out/link";
    allowedReferences = [ ];
    inherit dep;
  };

  test7 = makeTest 7 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $out $out/link";
    allowedReferences = [ "out" ];
    inherit dep;
  };

  test8 = makeTest 8 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s ${test1} $out/link";
    inherit dep;
  };

  test9 = makeTest 9 {
    builder = builtins.toFile "builder.sh" "mkdir $out; ln -s $dep $out/link";
    inherit dep;
    disallowedReferences = [ dep ];
  };

  test10 = makeTest 10 {
    builder = builtins.toFile "builder.sh" "mkdir $out; echo $test5; ln -s $dep $out/link";
    inherit dep test5;
    disallowedReferences = [ test5 ];
  };

  test11 = makeTest 11 {
    __structuredAttrs = true;
    unsafeDiscardReferences.out = true;
    outputChecks.out.allowedReferences = [ ];
    buildCommand = ''echo ${dep} > "''${outputs[out]}"'';
  };

  test12 = makeTest 12 {
    builder = builtins.toFile "builder.sh" "mkdir $out $lib";
    outputs = [
      "out"
      "lib"
    ];
    disallowedReferences = [ "dev" ];
  };

  # test13 and test14 are regression tests for a bug where an
  # `outputChecks.<output>.disallowedReferences` (or `allowedReferences`
  # etc.) entry that names a *sibling* output by symbolic name (e.g.
  # "out") failed to resolve -- with a spurious "not a valid output of
  # this derivation" error -- whenever that sibling output was already
  # valid before this build (e.g. GC-rooted by something else) and was
  # therefore not rebuilt/re-registered by this invocation. See
  # https://github.com/NixOS/nix/issues/16485.
  #
  # Both outputs are always built together (single derivation, single
  # builder run); what varies between the two rebuilds in check-refs.sh
  # is only which outputs were already *valid* going in.
  test13 = makeTest 13 {
    __structuredAttrs = true;
    outputs = [
      "out"
      "aux"
    ];
    # aux does not actually reference out; this only exercises symbolic
    # *resolution* of the "out" reference target.
    outputChecks.aux.disallowedReferences = [ "out" ];
    # This uses the raw-`derivation` `mkDerivation` above, not
    # stdenv's, so under __structuredAttrs outputs are only exposed via
    # the "outputs" bash associative array, not as bare $out/$aux.
    buildCommand = ''
      mkdir -p "''${outputs[out]}" "''${outputs[aux]}"
      echo out > "''${outputs[out]}/x"
      echo aux > "''${outputs[aux]}/y"
    '';
  };

  # Same shape as test13, but aux genuinely references out, so that
  # check-refs.sh can confirm that making "out" resolvable as a reference
  # target for aux's checks does not also weaken enforcement of aux's own
  # disallowedReferences.
  test14 = makeTest 14 {
    __structuredAttrs = true;
    outputs = [
      "out"
      "aux"
    ];
    outputChecks.aux.disallowedReferences = [ "out" ];
    buildCommand = ''
      mkdir -p "''${outputs[out]}" "''${outputs[aux]}"
      echo out > "''${outputs[out]}/x"
      ln -s "''${outputs[out]}" "''${outputs[aux]}/link"
    '';
  };

}
