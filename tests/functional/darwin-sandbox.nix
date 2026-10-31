{ storeDir }:

let
  inherit (import ./config.nix) mkDerivation;

  alphabet = "0123456789abcdfghijklmnpqrsvwxyz";

  # Build a 32-character store hash with a chosen first character.
  # E.g. makePath "0" 17 gives /.../0<31 hash chars>-sandbox-input-17.
  makePath =
    prefix: index:
    let
      hash = builtins.convertHash {
        hash = builtins.hashString "sha256" (toString index);
        hashAlgo = "sha256";
        toHashFormat = "nix32";
      };
    in
    "${storeDir}/${prefix}${builtins.substring 0 31 hash}-sandbox-input-${toString index}";

  # Repeat the supplied prefixes in order, wrapping at the end.
  # With "01", indices 0, 1, 2, 3 produce "0", "1", "0", "1".
  cyclePrefix =
    prefixes: index:
    let
      prefixCount = builtins.stringLength prefixes;
      wrappedIndex = index - (builtins.div index prefixCount) * prefixCount;
    in
    builtins.substring wrappedIndex 1 prefixes;

  makeFixtures =
    {
      prefixes,
      count,
      kind,
      startIndex ? 0,
    }:
    builtins.genList (index: {
      path = makePath (cyclePrefix prefixes index) (startIndex + index);
      inherit kind;
    }) count;

  makeTest = name: fixtures: {
    inherit fixtures;

    test = mkDerivation {
      name = "darwin-sandbox-${name}";
      inputs =
        builtins.concatStringsSep "\n" (map (fixture: builtins.storePath fixture.path) fixtures) + "\n";
      # Keep the long input list in a file to avoid environment size limits.
      passAsFile = [ "inputs" ];

      buildCommand = ''
        while IFS= read -r input; do
          path="$input"
          if [[ -d "$path" ]]; then
            path="$path/data"
          fi

          read -r contents < "$path"
          [[ "$contents" == fixture ]]

          # The setup creates this file beside the input, but does not declare it.
          if read -r contents 2>/dev/null < "$input-denied"; then
            echo "Sandbox allowed undeclared path: $input-denied" >&2
            exit 1
          fi
        done < "$inputsPath"

        echo success > "$out"
      '';
    };
  };
in
{
  ordering = makeTest "ordering" (
    # Create 700 directories with names starting with 0 and 700 with 1,
    # alternating: 0..., 1..., 0..., 1..., ...
    #
    # macOS is sensitive to the order of paths in a sandbox rule.
    # With this many paths, it accepts all 0... paths followed by all 1...
    # paths, but hits an internal size limit if they alternate.
    # Start with alternating paths to catch changes that stop Nix from
    # grouping them before writing the sandbox rules.
    makeFixtures {
      prefixes = "01";
      count = 1400;
      kind = "directory";
    }
    # Also exercise literal file filters and every first hash character.
    ++ makeFixtures {
      prefixes = alphabet;
      count = 32;
      kind = "file";

      # Keep file identifiers separate from the directories above.
      startIndex = 1400;
    }
  );

  # This profile uses about 460 KiB on macOS 26.5.1, leaving roughly 10%
  # below the 512-KiB boundary. Keep the input count fixed so profile changes
  # must preserve this capacity.
  capacity = makeTest "capacity" (makeFixtures {
    # Cycle 0..., 1..., ..., z..., 0..., ...: 250 directories per prefix.
    prefixes = alphabet;
    count = 8000;
    kind = "directory";
  });
}
