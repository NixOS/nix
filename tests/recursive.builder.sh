ln -sv $(nix-store -r $(nix-instantiate $simple)) $out
