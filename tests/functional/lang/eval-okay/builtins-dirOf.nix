{
  stringEmpty = dirOf "";
  stringNoSep = dirOf "filename";
  stringSingleDir = dirOf "a/b";
  stringMultipleSeps = dirOf "a///b";
  stringRoot = dirOf "/";
  stringRootSlash = dirOf "//";
  stringRootSlashSlash = dirOf "///";
  stringRootA = dirOf "/a";
  stringWithDot = dirOf "a/b/c/./d";
  stringWithDotSep2 = dirOf "a/b/c/.//d";
  stringWithDotDot = dirOf "a/b/c/../d";
  stringWithDotDotSep2 = dirOf "a/b/c/..//d";
  stringWithDotAndDotDot = dirOf "a/b/c/.././d";
  stringWithDotAndDotDotSep2 = dirOf "a/b/c/.././/d";

  pathRoot = dirOf /.;
  pathDoesntExistRoot = dirOf /totallydoesntexistreally;
  pathDoesntExistNested1 = dirOf /totallydoesntexistreally/subdir1;
  pathDoesntExistNested2 = dirOf /totallydoesntexistreally/subdir1/subdir2;
}
