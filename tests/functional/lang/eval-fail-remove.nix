let {
  attrs = {x = 123; y = 456;};

  body = (removeAttrs attrs ["x"]).x;
}