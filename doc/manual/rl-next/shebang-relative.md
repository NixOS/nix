---
synopsis: "`nix-shell` shebang uses relative path"
prs:
- 5088
- 11058
issues:
- 4232
---

<!-- unfortunately no link target for the specific syntax -->
Relative [path](@docroot@/language/values.md#type-path) literals in `nix-shell` shebang scripts' options are now resolved relative to the [script's location](@docroot@/glossary?highlight=base%20directory#gloss-base-directory).
Previously they were resolved relative to the current working directory.

For example, consider the following script in `~/myproject/say-hi`:

```shell
#!/usr/bin/env nix-shell
#!nix-shell --expr 'import ./shell.nix'
#!nix-shell --arg toolset './greeting-tools.nix'
#!nix-shell -i bash
hello
```

Older versions of `nix-shell` would resolve `shell.nix` relative to the current working directory; home in this example:

```console
[hostname:~]$ ./myproject/say-hi
error:
       … while calling the 'import' builtin
         at «string»:1:2:
            1| (import ./shell.nix)
             |  ^

       error: path '/home/user/shell.nix' does not exist
```

Since this release, `nix-shell` resolves `shell.nix` relative to the script's location, and `~/myproject/shell.nix` is used.

```console
$ ./myproject/say-hi
Hello, world!
```

**Opt-out**

This is technically a breaking change, so we have added an option so you can adapt independently of your Nix update.
The old behavior can be opted into by setting the option [`nix-shell-shebang-arguments-relative-to-script`](@docroot@/command-ref/conf-file.md#conf-nix-shell-shebang-arguments-relative-to-script) to `false`.
This option will be removed in a future release.

**`nix` command shebang**

The experimental [`nix` command shebang](@docroot@/command-ref/new-cli/nix.md?highlight=shebang#shebang-interpreter) already behaves in this script-relative manner.

Example:

```shell
#!/usr/bin/env nix
#!nix develop
#!nix --expr ``import ./shell.nix``
#!nix -c bash
hello
```
