synopsis: ensure nix-shell shebang uses relative path
prs: #5088
description: {

`nix-shell` shebangs use the script file's relative location to resolve relative paths to files passed as command line arguments, but expression arguments were still evaluated using the current working directory as a base path.
The new behavior is that evaluations are performed relative to the script.

}
