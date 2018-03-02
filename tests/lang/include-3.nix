if x > 0 then x * (let x_ = x; in let x = x_ - 1; in include ./include-3.nix) else 1
