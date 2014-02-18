nix-store -r $(nix-instantiate $simple) --add-root $out
