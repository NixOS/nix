R""(

# Examples

* Display all special commands within the REPL:

  ```console
  nix repl
  ```

  ```console
  nix-repl> :?
  ```

* Evaluate some simple Nix expressions:

  ```console
  nix repl
  ```

  ```console
  nix-repl> 1 + 2
  ```

      3

  ```console
  nix-repl> map (x: x * 2) [1 2 3]
  ```

      [ 2 4 6 ]

* Interact with Nixpkgs in the REPL:

  ```console
  nix repl --file example.nix
  ```

      Loading Installable ''...
      Added 3 variables.

  Evaluate 
  ```console
  nix repl --expr '{a={b=3;c=4;};}'
  ```

      Loading Installable ''...
      Added 1 variables.

  ```console
  nix repl --expr '{a={b=3;c=4;};}' a
  ```

      Loading Installable ''...
      Added 1 variables.

  ```console
  nix repl --extra-experimental-features 'flakes repl-flake' nixpkgs
  ```

      Loading Installable 'flake:nixpkgs#'...
      Added 5 variables.

  ```console
  nix-repl> legacyPackages.x86_64-linux.emacs.name
  ```

      "emacs-27.1"

  ```console
  nix-repl> legacyPackages.x86_64-linux.emacs.name
  ```

      "emacs-27.1"

  ```console
  nix-repl> :q
  ```

  ```console
  nix repl --expr 'import <nixpkgs>{}'
  ```

      Loading Installable ''...
      Added 12439 variables.

  ```console
  nix-repl> emacs.name
  ```

      "emacs-27.1"

  ```console
  nix-repl> emacs.drvPath
  ```

      "/nix/store/lp0sjrhgg03y2n0l10n70rg0k7hhyz0l-emacs-27.1.drv"

  ```console
  nix-repl> drv = runCommand "hello" { buildInputs = [ hello ]; } "hello; hello > $out"
  ```

  ```console
  nix-repl> :b drv
  ```

      this derivation produced the following outputs:
        out -> /nix/store/0njwbgwmkwls0w5dv9mpc1pq5fj39q0l-hello

  ```console
  nix-repl> builtins.readFile drv
  ```

      "Hello, world!\n"

  ```console
  nix-repl> :log drv
  ```

      Hello, world!

# Description

This command provides an interactive environment for evaluating Nix
expressions. (REPL stands for 'read–eval–print loop'.)

On startup, it loads the Nix expressions named *files* and adds them
into the lexical scope. You can load addition files using the `:l
<filename>` command, or reload all files using `:r`.

)""
