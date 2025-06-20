{
  missingAttr = let bar = { }; in bar.notExist;
  insideAList = [ (throw "a throw") ];
  deeper = { v = throw "v"; };
  failedAssertion = assert true; assert false; null;
  missingFile = builtins.readFile ./missing-file.txt;
  missingImport = import ./missing-import.nix;
  outOfBounds = builtins.elemAt [ 1 2 3 ] 100;
  failedCoersion = "${1}";
  failedAddition = 1.0 + "a string";
}
