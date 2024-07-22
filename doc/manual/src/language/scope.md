# Scoping rules

Nix is [statically scoped](https://en.wikipedia.org/wiki/Scope_(computer_science)#Lexical_scope), but with multiple scopes and shadowing rules.

* primary scope: explicitly-bound variables
  * [`let`](./syntax.md#let-expressions)
  * [`inherit`](./syntax.md#inheriting-attributes)
  * [function](./syntax.md#functions) arguments

* secondary scope: implicitly-bound variables
  * [`with`](./syntax.md#with-expressions)

Primary scope takes precedence over secondary scope.
See [`with`](./syntax.md#with-expressions) for a detailed example.
