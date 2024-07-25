# Scoping rules

A _scope_ in the Nix language is a [name]-keyed dictionary, mapping each name to an expression and a _definition type_.
The definition type is either _explicit_ or _implicit_.
Each entry in this dictionary is a _definition_.

[name]: identifiers.md#names

Explicit definitions are created by the following expressions:
  * [let-expressions](syntax.md#let-expressions)
  * [recursive attribute set literals](syntax.md#recursive-sets) (`rec`)
  * [function literals](syntax.md#functions)

Implicit definitions are only created by [with-expressions].

[with-expressions]: syntax.md#with-expressions

Every expression is _enclosed_ by a scope.
The outermost expression of a Nix language file is enclosed by the [global scope], which contains only explicit definitions.

[global scope]: builtins.md

The above expressions _extend_ their enclosing scope by adding new definitions, or replacing existing ones with the same name.
An explicit definition can replace a definition of any type; an implicit definition can only replace another implicit definition.
See [with-expressions] for a detailed example.

Each of the above expressions defines which of its subexpressions are enclosed by the extended scope.
In all other cases, the same scope that encloses an expression is the enclosing scope for its subexpressions.

The Nix language is [statically scoped]; the value of a variable is determined only by the variable's enclosing scope, and not by the dynamic context in which the variable is evaluated.

[statically scoped]: https://en.wikipedia.org/wiki/Scope_(computer_science)#Lexical_scope

> **Note**
>
> Expressions entered into the [Nix REPL] are enclosed by a scope that can be extended by command line arguments or previous REPL commands.
> These ways of extending scope are not, strictly speaking, part of the Nix language.

[Nix REPL]: @docroot@/command-ref/new-cli/nix3-repl.md
