source common.sh

nix build -f ../multiple-outputs.nix --json a.all b.all | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
  and (.[1] |
    (.drvPath | match(".*multiple-outputs-b.drv")) and
    (.outputs.out | match(".*multiple-outputs-b")))
'
