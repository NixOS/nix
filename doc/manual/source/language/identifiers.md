# Identifiers

An *identifier* is an [ASCII](https://en.wikipedia.org/wiki/ASCII) character sequence that:
- Starts with a letter (`a-z`, `A-Z`) or underscore (`_`)
- Can contain any number of:
  - Letters (`a-z`, `A-Z`)
  - Digits (`0-9`)
  - Underscores (`_`)
  - Apostrophes (`'`)
  - Hyphens (`-`)
- Is not one of the [keywords](#keywords)

> **Syntax**
>
> *identifier* ~ `[A-Za-z_][A-Za-z0-9_'-]*`

# Names

A *name* can be written as an [identifier](#identifiers) or a [string literal](./string-literals.md).

> **Syntax**
>
> *name* → *identifier* | *string*

Names are used in [attribute sets](./syntax.md#attrs-literal), [`let` bindings](./syntax.md#let-expressions), and [`inherit`](./syntax.md#inheriting-attributes).
Two names are the same if they represent the same sequence of characters, regardless of whether they are written as identifiers or strings.

# Keywords

These keywords are reserved and cannot be used as [identifiers](#identifiers):

- [`assert`](./syntax.md#assertions)
- [`else`][if]
- [`if`][if]
- [`in`][let]
- [`inherit`](./syntax.md#inheriting-attributes)
- [`let`][let]
- [`or`](./operators.md#attribute-selection) (see note)
- [`rec`](./syntax.md#recursive-sets)
- [`then`][if]
- [`with`](./syntax.md#with-expressions)

[if]: ./syntax.md#conditionals
[let]: ./syntax.md#let-expressions

> **Note**
>
> The Nix language evaluator currently allows `or` to be used as a name in some contexts, for backwards compatibility reasons.
> Users are advised not to rely on this.
>
> There are long-standing issues with how `or` is parsed as a name, which can't be resolved without making a breaking change to the language.
