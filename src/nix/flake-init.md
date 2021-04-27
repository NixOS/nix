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
template is `templates#defaultTemplate`, but this can be overridden
using `-t`.

# Template definitions

A flake can declare templates through its `templates` and
`defaultTemplate` output attributes. A template has two attributes:

* `description`: A one-line description of the template, in CommonMark
  syntax.

* `path`: The path of the directory to be copied.

Here is an example:

```
outputs = { self }: {

  templates.rust = {
    path = ./rust;
    description = "A simple Rust/Cargo project";
  };

  templates.defaultTemplate = self.templates.rust;
}
```

)""
