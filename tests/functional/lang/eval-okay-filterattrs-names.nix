builtins.filterAttrs (name: value: name == "a") {
  a = 3;
  b = 6;
  c = 10;
}
