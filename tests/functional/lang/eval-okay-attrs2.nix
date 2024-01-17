let {
  as = { x = 123; y = 456; } // { z = 789; } // { z = 987; };

  A = "a";
  Z = "z";

  body = if builtins.hasAttr A as
         then builtins.getAttr A as
         else assert builtins.hasAttr Z as; builtins.getAttr Z as;
}
