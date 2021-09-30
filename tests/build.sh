source common.sh

expectedJSONRegex='\[\{"drvPath":".*multiple-outputs-a.drv","outputs":\{"first":".*multiple-outputs-a-first","second":".*multiple-outputs-a-second"}},\{"drvPath":".*multiple-outputs-b.drv","outputs":\{"out":".*multiple-outputs-b"}}]'
nix build -f multiple-outputs.nix --json a.all b.all --no-link | jq --exit-status '
  (.[0] |
    (.drvPath | match(".*multiple-outputs-a.drv")) and
    (.outputs |
      (.first | match(".*multiple-outputs-a-first")) and
      (.second | match(".*multiple-outputs-a-second"))))
  and (.[1] |
    (.drvPath | match(".*multiple-outputs-b.drv")) and
    (.outputs.out | match(".*multiple-outputs-b")))
'
testNormalization () {
    clearStore
    outPath=$(nix-build ./simple.nix --no-out-link)
    test "$(stat -c %Y $outPath)" -eq 1
}

testNormalization
