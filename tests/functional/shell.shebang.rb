#! @SHELL_PROG@
#! ruby
#! nix-shell -I nixpkgs=shell.nix --no-substitute
#! nix-shell --pure -p ruby -i ruby

# Contents doesn't matter.
abort("This shouldn't be executed.")
