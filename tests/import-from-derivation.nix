with import ./config.nix;
import (
 mkDerivation {
   name = "foo";
   bla = import ./dependencies.nix;
   buildCommand = "
     echo \\\"hi\\\" > $out
   ";
 }
)
