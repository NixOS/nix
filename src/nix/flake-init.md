R""(

# Examples

* Create a flake using the default template:

  ```console
  # nix flake init
  ```

* List available templates:

  ```console
  # nix flake show templates
  ```

* Create a flake from a specific template:

  ```console
  # nix flake init -t templates#simpleContainer
  ```

# Description

This command creates a flake in the current directory by copying the
files of a template. It will not overwrite existing files. The default
template is `templates#templates.default`, but this can be overridden
using `-t`.

# Template definitions

A flake can declare templates through its `templates` output
attribute. A template has two attributes:

* `description`: A one-line description of the template, in CommonMark
  syntax.

* `path`: The path of the directory to be copied.

* `welcomeText`: A block of markdown text to display when a user initializes a
  new flake based on this template.


Here is an example:

```
outputs = { self }: {

  templates.rust = {
    path = ./rust;
    description = "A simple Rust/Cargo project";
    welcomeText = ''
      # Simple Rust/Cargo Template
      ## Intended usage
      The intended usage of this flake is...

      ## More info
      - [Rust language](https://www.rust-lang.org/)
      - [Rust on the NixOS Wiki](https://nixos.wiki/wiki/Rust)
      - ...
    '';
  };

  templates.default = self.templates.rust;
}
```

)""
