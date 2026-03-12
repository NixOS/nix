with import ./config.nix;
{
  # Deep chain: tests that paths can be written out of dependency
  # order and registered correctly as a batch.
  chain =
    let
      mk =
        name: dep:
        mkDerivation {
          inherit name;
          buildCommand = ''
            mkdir $out
            ${if dep == null then "" else "echo ${dep} > $out/dep"}
            echo ${name} > $out/name
          '';
        };
      p1 = mk "chain-1" null;
      p2 = mk "chain-2" p1;
      p3 = mk "chain-3" p2;
      p4 = mk "chain-4" p3;
      p5 = mk "chain-5" p4;
      p6 = mk "chain-6" p5;
      p7 = mk "chain-7" p6;
      p8 = mk "chain-8" p7;
    in
    p8;

  # Wide closure: 20 leaves + 1 top. With _NIX_TEST_CHUNK_SIZE=8
  # this spans 3 chunks; wide-top's references cross chunk
  # boundaries, exercising cross-chunk dependency resolution.
  wide =
    let
      mkLeaf =
        i:
        mkDerivation {
          name = "wide-${toString i}";
          buildCommand = "echo ${toString i} > $out";
        };
      leaves = builtins.genList mkLeaf 20;
    in
    mkDerivation {
      name = "wide-top";
      buildCommand = ''
        mkdir $out
        ${builtins.concatStringsSep "\n" (map (l: "echo ${l} >> $out/deps") leaves)}
      '';
    };

  # Dedup test: two derivations that each contain a file with the same
  # content.  After `nix copy` with auto-optimise-store, the files
  # should be hardlinked together (same inode).
  dedup-a = mkDerivation {
    name = "dedup-a";
    buildCommand = ''
      mkdir $out
      echo "identical-content-for-dedup-test" > $out/samefile
    '';
  };
  dedup-b = mkDerivation {
    name = "dedup-b";
    buildCommand = ''
      mkdir $out
      echo "identical-content-for-dedup-test" > $out/samefile
    '';
  };
}
