builtins.toJSON {
  a.b = [
    true
    false
    "it's a bird"
    {
      c.d = throw "hah no";
    }
  ];
}
