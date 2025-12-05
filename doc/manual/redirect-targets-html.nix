# Generates redirect-targets.html containing all redirect targets for link checking.
# Used by: doc/manual/package.nix (passthru.tests.linkcheck)

{
  stdenv,
  lib,
  jq,
}:

stdenv.mkDerivation {
  name = "redirect-targets-html";

  src = lib.fileset.toSource {
    root = ./.;
    fileset = lib.fileset.unions [
      ./source/_redirects
      ./redirects.json
    ];
  };

  nativeBuildInputs = [ jq ];

  installPhase = ''
    mkdir -p $out

    {
      echo '<!DOCTYPE html>'
      echo '<html><head><title>Nix Manual Redirect Targets</title></head><body>'
      echo '<h1>Redirect Targets to Check</h1>'
      echo '<p>This document contains all redirect targets from the Nix manual.</p>'

      echo '<h2>Server-side redirects (from _redirects)</h2>'
      echo '<ul>'

      # Extract targets from _redirects file (second field, skip comments and empty lines)
      grep -v '^#' source/_redirects | grep -v '^$' | while read -r source target code; do
        # Handle splat patterns by converting to a concrete example
        if [[ "$target" == *":splat"* ]]; then
          target="''${target//:splat/example}"
        fi
        # Remove leading slash for relative path
        target="''${target#/}"
        echo "<li><a href=\"$target\">$target</a> (from $source)</li>"
      done

      echo '</ul>'

      echo '<h2>Client-side redirects (from redirects.json)</h2>'
      echo '<ul>'

      # Extract all redirects with their source pages to properly resolve relative paths
      jq -r 'to_entries[] | .key as $page | .value | to_entries[] | "\($page)\t\(.value)"' \
        redirects.json | while IFS=$'\t' read -r page target; do

        page_dir=$(dirname "$page")

        # Handle fragment-only targets (e.g., #primitives)
        if [[ "$target" == \#* ]]; then
          # Fragment is on the same page
          resolved="$page$target"
          echo "<li><a href=\"$resolved\">$resolved</a> (fragment on $page)</li>"
          continue
        fi

        # Resolve relative path based on the source page location
        resolved="$page_dir/$target"

        echo "<li><a href=\"$resolved\">$resolved</a> (from $page)</li>"
      done

      echo '</ul>'
      echo '</body></html>'
    } > $out/redirect-targets.html

    echo "Generated redirect targets document with $(grep -c '<li>' $out/redirect-targets.html) links"
  '';

  meta = {
    description = "HTML document listing all Nix manual redirect targets for link checking";
  };
}
