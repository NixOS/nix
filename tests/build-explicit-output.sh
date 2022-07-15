source common.sh

set -o pipefail

drv=$(nix eval -f multiple-outputs.nix --raw a.drvPath)
if nix build "$drv^not-an-output" --no-link --json; then
    fail "'not-an-output' should fail to build"
fi

if nix build "$drv^" --no-link --json; then
    fail "'empty outputs list' should fail to build"
fi

if nix build "$drv^*nope" --no-link --json; then
    fail "'* must be entire string' should fail to build"
fi

nix build "$drv^first" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 1) and
      (.first | match(".*multiple-outputs-a-first")) and
      (has("second") | not)))
'

nix build "$drv^first,second" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
	  (.second | match(".*multiple-outputs-a-second"))))
'

nix build "$drv^*" --no-link --json | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (keys | length == 2) and
      (.first | match(".*multiple-outputs-a-first")) and
	  (.second | match(".*multiple-outputs-a-second"))))
'
