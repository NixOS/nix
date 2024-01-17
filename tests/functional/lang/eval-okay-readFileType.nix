{
  bar    = builtins.readFileType ./readDir/bar;
  foo    = builtins.readFileType ./readDir/foo;
  linked = builtins.readFileType ./readDir/linked;
  ldir   = builtins.readFileType ./readDir/ldir;
}
