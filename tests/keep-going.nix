with import ./config.nix;

rec {

  # Hack to get the scheduler to do what we want: The `good` derivation can
  # only be built after `delay_good` (which takes a long time to build) while
  # the others don't have any dependency.
  # This means that if we build this with parallelism (`-j2`), then we can be
  # reasonably sure that the failing derivations will be scheduled _before_ the
  # `good` one (and so we can check that `--keep-going` works fine)
  delay_good = mkDerivation {
    name = "delay-good";
    buildCommand = "sleep 3; touch $out";
  };

  good = mkDerivation {
    name = "good";
    buildCommand = "mkdir $out; echo foo > $out/bar";
    delay = delay_good;
  };

  failing = mkDerivation {
    name = "failing";
    buildCommand = false;
  };

  requiresFooSystemFeature = mkDerivation {
    name = "requires-foo-system-feature";
    buildCommand = "mkdir $out; echo foo > $out/bar";
    requiredSystemFeatures = [ "foo" ];
  };

}

