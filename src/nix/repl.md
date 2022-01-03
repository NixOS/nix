R""(

# Examples

* Display all special commands within the REPL:

  ```console
  # nix repl
  nix-repl> :?
  ```

* Evaluate some simple Nix expressions:

  ```console
  # nix repl

  nix-repl> 1 + 2
  3

  nix-repl> map (x: x * 2) [1 2 3]
  [ 2 4 6 ]
  ```

* Interact with Nixpkgs in the REPL:

  ```console
  # nix repl '<nixpkgs>'

  Loading '<nixpkgs>'...
  Added 12428 variables.

  nix-repl> emacs.name
  "emacs-27.1"

  nix-repl> emacs.drvPath
  "/nix/store/lp0sjrhgg03y2n0l10n70rg0k7hhyz0l-emacs-27.1.drv"

  nix-repl> drv = runCommand "hello" { buildInputs = [ hello ]; } "hello; hello > $out"

  nix-repl> :b drv
  this derivation produced the following outputs:
    out -> /nix/store/0njwbgwmkwls0w5dv9mpc1pq5fj39q0l-hello

  nix-repl> builtins.readFile drv
  "Hello, world!\n"

  nix-repl> :log drv
  Hello, world!
  ```

# Description

This command provides an interactive environment for evaluating Nix
expressions. (REPL stands for 'read–eval–print loop'.)

On startup, it loads the Nix expressions named *files* and adds them
into the lexical scope. You can load addition files using the `:l
<filename>` command, or reload all files using `:r`.

)""
