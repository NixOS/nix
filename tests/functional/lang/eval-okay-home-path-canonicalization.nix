{
  baseNameDot = baseNameOf ~/.;
  baseNameDotDot = baseNameOf ~/..;
  complexTraversal = ~/a/b/../../c;
  dirOfDot = dirOf ~/.;
  dirOfDotDot = dirOf ~/..;
  dot = ~/.;
  dotDot = ~/..;
  dotSlashDot = ~/./.;
  equalCanonical = ~/. == /fake-home;
  equalRoot = ~/.. == /.;
  equalSelf = ~/. == ~/./.;
  excessDotDot = ~/../../../../..;
  fooDotBar = ~/foo/./bar;
  fooDotDot = ~/foo/..;
  fooDotDotBar = ~/foo/../bar;
  pathAddAbsolute = ~/. + /foo;
  pathAddRoot = ~/.. + /foo;
  pathAddString = ~/. + "/foo";
  pathAddition = ~/. + ~/../../..;
  toStringDot = toString ~/.;
  toStringDotDot = toString ~/..;
}
