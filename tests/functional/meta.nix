with import ./config.nix;

rec {
  # Two derivations that differ only in __meta content
  # These should produce the SAME output path (quotient property)
  metaDiff1 = mkDerivation {
    name = "meta-test";
    __structuredAttrs = true;
    buildCommand = ''
      echo "Hello from meta test" > "''${outputs[out]}"
    '';
    __meta = {
      description = "First variant";
      version = "1.0";
    };
    requiredSystemFeatures = [ "derivation-meta" ];
  };

  metaDiff2 = mkDerivation {
    name = "meta-test";
    __structuredAttrs = true;
    buildCommand = ''
      echo "Hello from meta test" > "''${outputs[out]}"
    '';
    __meta = {
      description = "Second variant";
      version = "2.0";
      maintainer = "someone";
    };
    requiredSystemFeatures = [ "derivation-meta" ];
  };

  # Derivation with __meta but WITHOUT derivation-meta system feature
  # This should NOT filter __meta, so it has a different output path than metaDiff1
  withoutSystemFeature = mkDerivation {
    name = "meta-test";
    __structuredAttrs = true;
    buildCommand = ''
      echo "Hello from meta test" > "''${outputs[out]}"
    '';
    __meta = {
      description = "First variant";
      version = "1.0";
    };
    requiredSystemFeatures = [ ];
  };

  # Traditional derivation without structured attributes
  # __meta is just a regular environment variable here, not filtered
  withoutStructuredAttrs = mkDerivation {
    name = "meta-test-traditional";
    buildCommand = ''
      echo "Traditional derivation" > $out
    '';
    __meta = "just a string";
  };

  # Derivation where derivation-meta is in systemFeatures along with other features
  # Verify that only derivation-meta is filtered, not the others
  metaWithOtherFeatures = mkDerivation {
    name = "meta-test";
    __structuredAttrs = true;
    buildCommand = ''
      echo "Hello from meta test" > "''${outputs[out]}"
    '';
    __meta = {
      description = "With other features";
    };
    requiredSystemFeatures = [
      "big-parallel"
      "derivation-meta"
      "benchmark"
    ];
  };

  # Derivation with empty __meta - should still work
  emptyMeta = mkDerivation {
    name = "meta-test";
    __structuredAttrs = true;
    buildCommand = ''
      echo "Hello from meta test" > "''${outputs[out]}"
    '';
    __meta = { };
    requiredSystemFeatures = [ "derivation-meta" ];
  };

  # Test whether adding derivation-meta system feature changes output path
  # Derivation without requiredSystemFeatures attribute at all
  withoutRequiredSystemFeatures = mkDerivation {
    name = "meta-test";
    __structuredAttrs = true;
    buildCommand = ''
      echo "Hello from meta test" > "''${outputs[out]}"
    '';
  };

  # Derivation with ONLY derivation-meta in requiredSystemFeatures
  # After filtering, requiredSystemFeatures should be removed entirely (not set to [])
  # This should have the same output path as withoutRequiredSystemFeatures
  withOnlyDerivationMeta = mkDerivation {
    name = "meta-test";
    __structuredAttrs = true;
    buildCommand = ''
      echo "Hello from meta test" > "''${outputs[out]}"
    '';
    __meta = {
      description = "Test";
    };
    requiredSystemFeatures = [ "derivation-meta" ];
  };

  # Test that __meta doesn't leak into the builder
  # This derivation will fail if __meta is present in the builder environment
  metaNotInBuilder = mkDerivation {
    name = "meta-no-leak-test";
    __structuredAttrs = true;
    buildCommand = ''
      # Read the .attrs.json file that contains structured attributes
      if grep -q '"__meta"' .attrs.json; then
        echo "ERROR: __meta leaked into builder!" >&2
        echo "Contents of .attrs.json:" >&2
        cat .attrs.json >&2
        exit 1
      fi
      echo "OK: __meta not present in builder" > "''${outputs[out]}"
    '';
    __meta = {
      description = "This should not leak to builder";
      version = "1.0";
      maintainer = "test";
    };
    requiredSystemFeatures = [ "derivation-meta" ];
  };
}
