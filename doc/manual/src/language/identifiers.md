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

# Keywords

These keywords are reserved and cannot be used as [identifiers](#identifiers):

- [`if`][if]
- [`then`][if]
- [`else`][if]
- [`let`][let]
- [`in`][let]
- [`assert`](./syntax.md#assertions)
- [`inherit`](./syntax.md#inheriting-attributes)
- [`rec`](./syntax.md#recursive-sets)
- [`with`](./syntax.md#with-expressions)
- [`or`](./operators.md#attribute-selection) (see note)

[if]: ./syntax.md#conditionals
[let]: ./syntax.md#let-expressions

> **Note**
>
> The Nix language evaluator currently allows `or` to be used as a name in some contexts, for backwards compatibility reasons.
> Users are advised not to rely on this.
>
> There are long-standing issues with how `or` is parsed as a name, which can't be resolved without making a breaking change to the language.
