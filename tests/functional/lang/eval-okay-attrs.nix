let {
  as = { x = 123; y = 456; } // { z = 789; } // { z = 987; };

  body = if as ? a then as.a else assert as ? z; as.z;
}
