with import ./config.nix;

rec {

  f2 = dummy: builder: mode: algo: hash: mkDerivation {
    name = "fixed";
    inherit builder;
    outputHashMode = mode;
    outputHashAlgo = algo;
    outputHash = hash;
    inherit dummy;
    impureEnvVars = ["IMPURE_VAR1" "IMPURE_VAR2"];
  };

  f = f2 "";

  good = [
    (f ./fixed.builder1.sh "flat" "md5" "8ddd8be4b179a529afa5f2ffae4b9858")
    (f ./fixed.builder1.sh "flat" "sha1" "a0b65939670bc2c010f4d5d6a0b3e4e4590fb92b")
    (f ./fixed.builder2.sh "recursive" "md5" "3670af73070fa14077ad74e0f5ea4e42")
    (f ./fixed.builder2.sh "recursive" "sha1" "vw46m23bizj4n8afrc0fj19wrp7mj3c0")
  ];

  # Expression to test that `nix-build --check` also throws an error if the hash of
  # fixed-output derivation has changed even if the hash exists in the store (in this
  # case the hash exists because of `fixed.builder2.sh`, but building a derivation
  # with the same hash and a different result must throw an error).
  check = [
    (f ./fixed.builder1.sh "recursive" "md5" "3670af73070fa14077ad74e0f5ea4e42")
  ];

  good2 = [
    # Yes, this looks fscked up: builder2 doesn't have that result.
    # But Nix sees that an output with the desired hash already
    # exists, and will refrain from building it.
    (f ./fixed.builder2.sh "flat" "md5" "8ddd8be4b179a529afa5f2ffae4b9858")
  ];

  sameAsAdd =
    f ./fixed.builder2.sh "recursive" "sha256" "1ixr6yd3297ciyp9im522dfxpqbkhcw0pylkb2aab915278fqaik";

  bad = [
    (f ./fixed.builder1.sh "flat" "md5" "0ddd8be4b179a529afa5f2ffae4b9858")
  ];

  reallyBad = [
    # Hash too short, and not base-32 either.
    (f ./fixed.builder1.sh "flat" "md5" "ddd8be4b179a529afa5f2ffae4b9858")
  ];

  badReferences = mkDerivation rec {
    name = "bad-hash";
    builder = script;
    script = builtins.toFile "installer.sh" "echo $script >$out";
    outputHash = "1ixr6yd3297ciyp9im522dfxpqbkhcw0pylkb2aab915278fqaik";
    outputHashAlgo = "sha256";
    outputHashMode = "flat";
  };

  # Test for building two derivations in parallel that produce the
  # same output path because they're fixed-output derivations.
  parallelSame = [
    (f2 "foo" ./fixed.builder2.sh "recursive" "md5" "3670af73070fa14077ad74e0f5ea4e42")
    (f2 "bar" ./fixed.builder2.sh "recursive" "md5" "3670af73070fa14077ad74e0f5ea4e42")
  ];

  # Can use "nar" instead of "recursive" now.
  nar-not-recursive = f2 "foo" ./fixed.builder2.sh "nar" "md5" "3670af73070fa14077ad74e0f5ea4e42";
}
