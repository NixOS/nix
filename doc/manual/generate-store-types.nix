let
  inherit (builtins)
    attrNames
    listToAttrs
    concatStringsSep
    readFile
    replaceStrings
    ;
  showSettings = import <nix/generate-settings.nix>;
  showStoreDocs = import <nix/generate-store-info.nix>;
in

storeInfo:

let
  storesList = showStoreDocs {
    inherit storeInfo;
    inlineHTML = true;
  };

  index =
    let
      showEntry = store: "- [${store.name}](./${store.filename})";
    in
    concatStringsSep "\n" (map showEntry storesList);

  "index.md" = replaceStrings [ "@store-types@" ] [ index ] (
    readFile ./source/store/types/index.md.in
  );

  tableOfContents =
    let
      showEntry = store: "    - [${store.name}](store/types/${store.filename})";
    in
    concatStringsSep "\n" (map showEntry storesList) + "\n";

  "SUMMARY.md" = tableOfContents;

  storePages = listToAttrs (
    map (s: {
      name = s.filename;
      value = s.page;
    }) storesList
  );

in
storePages // { inherit "index.md" "SUMMARY.md"; }
