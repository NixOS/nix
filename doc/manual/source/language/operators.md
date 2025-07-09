# Operators

| Name                                   | Syntax                                     | Associativity | Precedence |
|----------------------------------------|--------------------------------------------|---------------|------------|
| [Attribute selection]                  | *attrset* `.` *attrpath* \[ `or` *expr* \] | none          | 1          |
| [Function application]                 | *func* *expr*                              | left          | 2          |
| [Arithmetic negation][arithmetic]      | `-` *number*                               | none          | 3          |
| [Has attribute]                        | *attrset* `?` *attrpath*                   | none          | 4          |
| List concatenation                     | *list* `++` *list*                         | right         | 5          |
| [Multiplication][arithmetic]           | *number* `*` *number*                      | left          | 6          |
| [Division][arithmetic]                 | *number* `/` *number*                      | left          | 6          |
| [Subtraction][arithmetic]              | *number* `-` *number*                      | left          | 7          |
| [Addition][arithmetic]                 | *number* `+` *number*                      | left          | 7          |
| [String concatenation]                 | *string* `+` *string*                      | left          | 7          |
| [Path concatenation]                   | *path* `+` *path*                          | left          | 7          |
| [Path and string concatenation]        | *path* `+` *string*                        | left          | 7          |
| [String and path concatenation]        | *string* `+` *path*                        | left          | 7          |
| Logical negation (`NOT`)               | `!` *bool*                                 | none          | 8          |
| [Update]                               | *attrset* `//` *attrset*                   | right         | 9          |
| [Less than][Comparison]                | *expr* `<` *expr*                          | none          | 10         |
| [Less than or equal to][Comparison]    | *expr* `<=` *expr*                         | none          | 10         |
| [Greater than][Comparison]             | *expr* `>` *expr*                          | none          | 10         |
| [Greater than or equal to][Comparison] | *expr* `>=` *expr*                         | none          | 10         |
| [Equality]                             | *expr* `==` *expr*                         | none          | 11         |
| Inequality                             | *expr* `!=` *expr*                         | none          | 11         |
| Logical conjunction (`AND`)            | *bool* `&&` *bool*                         | left          | 12         |
| Logical disjunction (`OR`)             | *bool* <code>\|\|</code> *bool*            | left          | 13         |
| [Logical implication]                  | *bool* `->` *bool*                         | right         | 14         |
| [Pipe operator] (experimental)         | *expr* `\|>` *func*                        | left          | 15         |
| [Pipe operator] (experimental)         | *func* `<\|` *expr*                        | right         | 15         |

[string]: ./types.md#type-string
[path]: ./types.md#type-path
[number]: ./types.md#type-float
[list]: ./types.md#type-list
[attribute set]: ./types.md#type-attrs

<!-- TODO(@rhendric, #10970): ^ rationalize number -> int/float -->

## Attribute selection

> **Syntax**
>
> *attrset* `.` *attrpath* \[ `or` *expr* \]

Select the attribute denoted by attribute path *attrpath* from [attribute set] *attrset*.
If the attribute doesn’t exist, return the *expr* after `or` if provided, otherwise abort evaluation.

[Attribute selection]: #attribute-selection

## Function application

> **Syntax**
>
> *func* *expr*

Apply the callable value *func* to the argument *expr*. Note the absence of any visible operator symbol.
A callable value is either:
- a [user-defined function][function]
- a [built-in][builtins] function
- an attribute set with a [`__functor` attribute](./syntax.md#attr-__functor)

> **Warning**
>
> [List][list] items are also separated by whitespace, which means that function calls in list items must be enclosed by parentheses.

## Has attribute

> **Syntax**
>
> *attrset* `?` *attrpath*

Test whether [attribute set] *attrset* contains the attribute denoted by *attrpath*.
The result is a [Boolean] value.

See also: [`builtins.hasAttr`](@docroot@/language/builtins.md#builtins-hasAttr)

[Boolean]: ./types.md#type-bool

[Has attribute]: #has-attribute

After evaluating *attrset* and *attrpath*, the computational complexity is O(log(*n*)) for *n* attributes in the *attrset*

## Arithmetic

Numbers will retain their type unless mixed with other numeric types:
Pure integer operations will always return integers, whereas any operation involving at least one floating point number returns a floating point number.

Evaluation of the following numeric operations throws an evaluation error:
- Division by zero
- Integer overflow, that is, any operation yielding a result outside of the representable range of [Nix language integers](./syntax.md#number-literal)

See also [Comparison] and [Equality].

The `+` operator is overloaded to also work on strings and paths.

[arithmetic]: #arithmetic

## String concatenation

> **Syntax**
>
> *string* `+` *string*

Concatenate two [strings][string] and merge their [string contexts](./string-context.md).

[String concatenation]: #string-concatenation

## Path concatenation

> **Syntax**
>
> *path* `+` *path*

Concatenate two [paths][path].
The result is a path.

[Path concatenation]: #path-concatenation

## Path and string concatenation

> **Syntax**
>
> *path* + *string*

Concatenate *[path]* with *[string]*.
The result is a path.

> **Note**
>
> The string must not have a [string context](./string-context.md) that refers to a [store path].

[Path and string concatenation]: #path-and-string-concatenation

## String and path concatenation

> **Syntax**
>
> *string* + *path*

Concatenate *[string]* with *[path]*.
The result is a string.

> **Important**
>
> The file or directory at *path* must exist and is copied to the [store].
> The path appears in the result as the corresponding [store path].

[store path]: @docroot@/store/store-path.md
[store]: @docroot@/glossary.md#gloss-store

[String and path concatenation]: #string-and-path-concatenation

## Update

> **Syntax**
>
> *attrset1* // *attrset2*

Update [attribute set] *attrset1* with names and values from *attrset2*.

The returned attribute set will have all of the attributes in *attrset1* and *attrset2*.
If an attribute name is present in both, the attribute value from the latter is taken.

[Update]: #update

## Comparison

Comparison is

- [arithmetic] for [numbers][number]
- lexicographic for [strings][string] and [paths][path]
- item-wise lexicographic for [lists][list]:
  elements at the same index in both lists are compared according to their type and skipped if they are equal.

All comparison operators are implemented in terms of `<`, and the following equivalencies hold:

| comparison   | implementation        |
|--------------|-----------------------|
| *a* `<=` *b* | `! (` *b* `<` *a* `)` |
| *a* `>`  *b* |       *b* `<` *a*     |
| *a* `>=` *b* | `! (` *a* `<` *b* `)` |

[Comparison]: #comparison

## Equality

- [Attribute sets][attribute set] and [lists][list] are compared recursively, and therefore are fully evaluated.
- Comparison of [functions][function] always returns `false`.
- Numbers are type-compatible, see [arithmetic] operators.
- Floating point numbers only differ up to a limited precision.

[function]: ./syntax.md#functions

[Equality]: #equality

## Logical implication

Equivalent to `!`*b1* `||` *b2*  (or `if` *b1* `then` *b2* `else true`)

[Logical implication]: #logical-implication

## Pipe operators

- *a* `|>` *b* is equivalent to *b* *a*
- *a* `<|` *b* is equivalent to *a* *b*

> **Example**
>
> ```
> nix-repl> 1 |> builtins.add 2 |> builtins.mul 3
> 9
>
> nix-repl> builtins.add 1 <| builtins.mul 2 <| 3
> 7
> ```

> **Warning**
>
> This syntax is part of an
> [experimental feature](@docroot@/development/experimental-features.md)
> and may change in future releases.
>
> To use this syntax, make sure the
> [`pipe-operators` experimental feature](@docroot@/development/experimental-features.md#xp-feature-pipe-operators)
> is enabled.
> For example, include the following in [`nix.conf`](@docroot@/command-ref/conf-file.md):
>
> ```
> extra-experimental-features = pipe-operators
> ```

[Pipe operator]: #pipe-operators
[builtins]: ./builtins.md
[Function application]: #function-application
