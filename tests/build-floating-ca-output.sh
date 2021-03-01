source common.sh

sed -i 's/experimental-features .*/& ca-derivations/' "$NIX_CONF_DIR"/nix.conf

nix build -f multiple-outputs.nix --arg floatingCA true --json a.all b.all | jq

nix build -f multiple-outputs.nix --arg floatingCA true --json a.all b.all | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
  and (.[1] |
    (.drvPath | match(".*multiple-outputs-b.drv")) and
    (.outputs.out | match(".*multiple-outputs-b")))
'
