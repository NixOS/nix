# The function ‘range’ comes from lib.nix and was added to the lexical
# scope by scopedImport.
range 1 5 ++ import ./imported2.nix
