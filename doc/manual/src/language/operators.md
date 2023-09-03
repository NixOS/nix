# Operators

| Name                                   | Syntax                                     | Associativity | Precedence |
|----------------------------------------|--------------------------------------------|---------------|------------|
| [Attribute selection]                  | *attrset* `.` *attrpath* \[ `or` *expr* \] | none          | 1          |
| Function application                   | *func* *expr*                              | left          | 2          |
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
| Logical disjunction (`OR`)             | *bool* `||` *bool*                         | left          | 13         |
| [Logical implication]                  | *bool* `->` *bool*                         | none          | 14         |

[string]: ./values.md#type-string
[path]: ./values.md#type-path
[number]: ./values.md#type-number
[list]: ./values.md#list
[attribute set]: ./values.md#attribute-set

## Attribute selection

Select the attribute denoted by attribute path *attrpath* from [attribute set] *attrset*.
If the attribute doesn’t exist, return *value* if provided, otherwise abort evaluation.

<!-- FIXME: the following should to into its own language syntax section, but that needs more work to fit in well -->

An attribute path is a dot-separated list of attribute names.
An attribute name can be an identifier or a string.

> *attrpath* = *name* [ `.` *name* ]...
> *name* = *identifier* | *string*
> *identifier* ~ `[a-zA-Z_][a-zA-Z0-9_'-]*`

[Attribute selection]: #attribute-selection

## Has attribute

> *attrset* `?` *attrpath*

Test whether [attribute set] *attrset* contains the attribute denoted by *attrpath*.
The result is a [Boolean] value.

[Boolean]: ./values.md#type-boolean

[Has attribute]: #has-attribute

## Arithmetic

Numbers are type-compatible:
Pure integer operations will always return integers, whereas any operation involving at least one floating point number return a floating point number.

See also [Comparison] and [Equality].

The `+` operator is overloaded to also work on strings and paths.

[arithmetic]: #arithmetic

## String concatenation

> *string* `+` *string*

Concatenate two [string]s and merge their string contexts.

[String concatenation]: #string-concatenation

## Path concatenation

> *path* `+` *path*

Concatenate two [path]s.
The result is a path.

[Path concatenation]: #path-concatenation

## Path and string concatenation

> *path* + *string*

Concatenate *[path]* with *[string]*.
The result is a path.

> **Note**
>
> The string must not have a string context that refers to a [store path].

[Path and string concatenation]: #path-and-string-concatenation

## String and path concatenation

> *string* + *path*

Concatenate *[string]* with *[path]*.
The result is a string.

> **Important**
>
> The file or directory at *path* must exist and is copied to the [store].
> The path appears in the result as the corresponding [store path].

[store path]: ../glossary.md#gloss-store-path
[store]: ../glossary.md#gloss-store

[Path and string concatenation]: #path-and-string-concatenation

## Update

> *attrset1* + *attrset2*

Update [attribute set] *attrset1* with names and values from *attrset2*.

The returned attribute set will have of all the attributes in *e1* and *e2*.
If an attribute name is present in both, the attribute value from the former is taken.

[Update]: #update

## Comparison

Comparison is

- [arithmetic] for [number]s 
- lexicographic for [string]s and [path]s
- item-wise lexicographic for [list]s:
  elements at the same index in both lists are compared according to their type and skipped if they are equal.

All comparison operators are implemented in terms of `<`, and the following equivalencies hold:

| comparison   | implementation        |
|--------------|-----------------------|
| *a* `<=` *b* | `! (` *b* `<` *a* `)` |
| *a* `>`  *b* |       *b* `<` *a*     |
| *a* `>=` *b* | `! (` *a* `<` *b* `)` |

[Comparison]: #comparison-operators

## Equality

- [Attribute sets][attribute set] and [list]s are compared recursively, and therefore are fully evaluated.
- Comparison of [function]s always returns `false`.
- Numbers are type-compatible, see [arithmetic] operators.
- Floating point numbers only differ up to a limited precision.

[function]: ./constructs.md#functions

[Equality]: #equality

## Logical implication

Equivalent to `!`*b1* `||` *b2*.

[Logical implication]: #logical-implication

