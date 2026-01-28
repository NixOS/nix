builtins.filterAttrs (name: value: value > 5) {
  a = 3;
  b = 6;
  c = 10;
}
