let

  attr = {
    "key 1" = "test";
    "key 2" = "caseok";
  };

  t1 = builtins.getAttr "key 1" attr;
  t2 = attr."key 2";
  t3 = attr ? "key 1";
  t4 = builtins.attrNames { inherit (attr) "key 1"; };

  # This is permitted, but there is currently no way to reference this
  # variable.
  "foo bar" = 1;

in t1 == "test"
   && t2 == "caseok"
   && t3 == true
   && t4 == ["key 1"]
