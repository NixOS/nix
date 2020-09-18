with builtins;

flake:

let

  splitLines = s: filter (x: !isList x) (split "\n" s);

  concatStrings = concatStringsSep "";

  modules = flake.modules or {};

in

rec {
  markdown =
    # FIXME: split into multiple files.

    "# Outputs\n\n"

    + concatStrings (map
      (outputName:
        "  - `${outputName}`  \n\n")
      (attrNames flake.outputs))

    + (if modules != {} then
      "# Modules\n\n"
      + concatStrings (map
        (moduleName:
          let
            module = modules.${moduleName};
          in
          "## `${moduleName}`\n\n"
          + (if module._module.doc or "" != ""
             then "### Synopsis\n\n${module._module.doc}\n\n"
             else "")
          + (if module._module.options != {}
             then
               "### Options\n\n"
               + concatStrings (map
                 (optionName:
                   let option = module._module.options.${optionName}; in
                   "  - `${optionName}`  \n\n"
                   + concatStrings (map (l: "    ${l}\n") (splitLines option.doc))
                   + "\n"
                 )
                 (attrNames module._module.options))
             else "")
        )
        (attrNames modules))
       else "");

  mdbook =
    let
      nixpkgs = getFlake "nixpkgs";
      pkgs = nixpkgs.legacyPackages.x86_64-linux; # FIXME
    in
      pkgs.runCommand "flake-doc"
        { buildInputs = [ pkgs.mdbook ];
        }
        ''
          mkdir $out
          mkdir -p book/src

          cat > book/book.toml <<EOF
          [output.html]
          additional-css = ["custom.css"]
          EOF

          cat > book/custom.css <<EOF
          h1:not(:first-of-type) {
              margin-top: 1.3em;
          }

          h2 {
              margin-top: 1em;
          }

          h3 {
              margin-top: 0.7em;
          }
          EOF

          cat > book/src/SUMMARY.md <<EOF
          # Table of Contents

          - [Overview](overview.md)
          EOF

          cp ${toFile "flake.md" markdown} book/src/overview.md

          mdbook build -d $out book
        '';
}
