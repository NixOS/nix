let {

  as = {
    x = 123;
    y = 456;
  };

  bs = rec {
    x = 789;
    inherit (as) x;
  };
  
}
