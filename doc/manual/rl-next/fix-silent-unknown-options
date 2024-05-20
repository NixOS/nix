---
synopsis: Warn on unknown settings anywhere in the command line
prs: 10701
---

All `nix` commands will now properly warn when an unknown option is specified anywhere in the command line.

Before:

```console
$ nix-instantiate --option foobar baz --expr '{}'
warning: unknown setting 'foobar'
$ nix-instantiate '{}' --option foobar baz --expr
$ nix eval --expr '{}' --option foobar baz
{ }
```

After:

```console
$ nix-instantiate --option foobar baz --expr '{}'
warning: unknown setting 'foobar'
$ nix-instantiate '{}' --option foobar baz --expr
warning: unknown setting 'foobar'
$ nix eval --expr '{}' --option foobar baz
warning: unknown setting 'foobar'
{ }
```
