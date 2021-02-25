R""(

# Examples

* Show the contents of all registries:

  ```console
  # nix registry list
  user   flake:dwarffs github:edolstra/dwarffs/d181d714fd36eb06f4992a1997cd5601e26db8f5
  system flake:nixpkgs path:/nix/store/fxl9mrm5xvzam0lxi9ygdmksskx4qq8s-source?lastModified=1605220118&narHash=sha256-Und10ixH1WuW0XHYMxxuHRohKYb45R%2fT8CwZuLd2D2Q=&rev=3090c65041104931adda7625d37fa874b2b5c124
  global flake:blender-bin github:edolstra/nix-warez?dir=blender
  global flake:dwarffs github:edolstra/dwarffs
  …
  ```

# Description

This command displays the contents of all registries on standard
output. Each line represents one registry entry in the format *type*
*from* *to*, where *type* denotes the registry containing the entry:

* `flags`: entries specified on the command line using `--override-flake`.
* `user`: the user registry.
* `system`: the system registry.
* `global`: the global registry.

See the [`nix registry` manual page](./nix3-registry.md) for more details.

)""
