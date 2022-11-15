# Release X.Y (202?-??-??)

* The experimental nix command is now a `#!-interpreter` by appending the
  contents of any `#! nix` lines and the script's location to a single call.
  Some examples:
  ```
  #!/usr/bin/env nix
  #! nix shell --file "<nixpkgs>" hello --command bash

  hello | cowsay
  ```
  or with flakes:
  ```
  #!/usr/bin/env nix
  #! nix shell nixpkgs#bash nixpkgs#hello nixpkgs#cowsay --command bash

  hello | cowsay
  ```
  or
  ```bash
  #! /usr/bin/env nix
  #! nix shell --impure --expr
  #! nix "with (import (builtins.getFlake ''nixpkgs'') {}); terraform.withPlugins (plugins: [ plugins.openstack ])"
  #! nix --command bash

  terraform "$@"
  ```
  or
  ```
  #!/usr/bin/env nix
  //! ```cargo
  //! [dependencies]
  //! time = "0.1.25"
  //! ```
  /*
  #!nix shell nixpkgs#rustc nixpkgs#rust-script nixpkgs#cargo --command rust-script
  */
  fn main() {
      for argument in std::env::args().skip(1) {
          println!("{}", argument);
      };
      println!("{}", std::env::var("HOME").expect(""));
      println!("{}", time::now().rfc822z());
  }
  // vim: ft=rust
  ```
