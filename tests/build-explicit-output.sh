source common.sh

drv=$(nix eval -f multiple-outputs.nix --raw a.drvPath)
if nix build "$drv!not-an-output" --json; then
    fail "'not-an-output' should fail to build"
fi

nix build "$drv!first" --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 1) and
      (.first | match(".*multiple-outputs-a-first")) and
      (has("second") | not)))
'
