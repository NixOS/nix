let {

  a = "let-a";

  as = {
    a = "as-a";
    b = "as-b";
  };

  bs = {
    a = "bs-a";
    b = "bs-b";
  };

  x = as.(a + " " + b);

  y = as.(bs.(a + " " + b));

  body = "x: (" + x + ") y: (" + y + ")";
}
