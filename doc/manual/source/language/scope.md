# Scoping rules

A *scope* in the Nix language is a dictionary keyed by [name](./identifiers.md#names), mapping each name to an expression and a *definition type*.
The definition type is either *explicit* or *implicit*.
Each entry in this dictionary is a *definition*.

Explicit definitions are created by the following expressions:
- [let-expressions](syntax.md#let-expressions)
- [recursive attribute set literals](syntax.md#recursive-sets) (`rec`)
- [function literals](syntax.md#functions)

Implicit definitions are only created by [with-expressions](./syntax.md#with-expressions).

Every expression is *enclosed* by a scope.
The outermost expression is enclosed by the [built-in, global scope](./builtins.md), which contains only explicit definitions.
The expressions listed above *extend* their enclosing scope by adding new definitions, or replacing existing ones with the same name.
An explicit definition can replace a definition of any type; an implicit definition can only replace another implicit definition.

Each of the above expressions defines which of its subexpressions are enclosed by the extended scope.
In all other cases, the same scope that encloses an expression is the enclosing scope for its subexpressions.

The Nix language is [statically scoped](https://en.wikipedia.org/wiki/Scope_(computer_science)#Lexical_scope);
the value of a variable is determined only by the variable's enclosing scope, and not by the dynamic context in which the variable is evaluated.

> **Note**
>
> Expressions entered into the [Nix REPL](@docroot@/command-ref/new-cli/nix3-repl.md) are enclosed by a scope that can be extended by command line arguments or previous REPL commands.
> These ways of extending scope are not, strictly speaking, part of the Nix language.
