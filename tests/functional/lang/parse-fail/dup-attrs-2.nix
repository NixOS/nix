let {

  as = {
    x = 123;
    y = 456;
  };

  bs = {
    x = 789;
    inherit (as) x;
  };
  
}
