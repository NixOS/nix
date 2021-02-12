source common.sh

expectedJSONRegex='\[\{"drvPath":".*multiple-outputs-a.drv","outputs":\{"first":".*multiple-outputs-a-first","second":".*multiple-outputs-a-second"}},\{"drvPath":".*multiple-outputs-b.drv","outputs":\{"out":".*multiple-outputs-b"}}]'
nix build -f multiple-outputs.nix --json a.all b.all | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
  and (.[1] |
    (.drvPath | match(".*multiple-outputs-b.drv")) and
    (.outputs.out | match(".*multiple-outputs-b")))
'
