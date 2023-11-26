synopsis: ensure nix-shell shebang uses relative path
prs: #5088
description: {

`nix-shell` shebangs use relative paths for files, but expressions were still evaluated using the current working directory. The new behavior is that evalutations are performed relative to the script.

}
