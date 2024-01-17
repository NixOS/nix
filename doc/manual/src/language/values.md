# Data Types

## Primitives

- <a id="type-string" href="#type-string">String</a>

> *string* = *double-quoted* | *indented* | *bare-uri*
>
> *double-quoted* = `"` (*char* | *doubleq-escaped* | *interpolated-expr*)* `"`
>
> *doubleq-escaped* = `\` (`n` | `r` | `t` | `"` | `${`)
>
> *[interpolated-expr]* = `${` *string-valued-expression* `}`
>
> *multiline* = *double-quoted*
>
> *indented* = `''` (*char* | *indented-escaped* | *interpolated-expr*)* `''`
>
> *indented-escaped* = `'` `''` | `''` `${` | `''\` (`n` | `r` | `t` | `\`) | `$` `$`

  *[interpolated-expr]*: ./string-interpolation.md

#### Quoted string

> *double-quoted* = `"` (*char* | *doubleq-escaped* | *interpolated-expr*)* `"`
>
> *doubleq-escaped* = `\` (`n` | `r` | `t` | `"` | `${` | `\`)

The following characters require escaping in quoted strings:
* `\n`: newline
* `\r`: carriage return
* `\t`: tab
* `"`: double quote
* `${`: start of a [string interpolation] block

#### Indented string

> *indented* = `''` (*char* | *indented-escaped* | *interpolated-expr*)* `''`
>
> *indented-escaped* = `'` `''` | `''` `${` | `''\` (`n` | `r` | `t` | `\`) | `$` `$`

Indeting strings automatically strip the following characters for convenience (an "empty line" contains only whitespace):
* from the first line: if an empty line, the first newline character
* from every line: whitespace common to start of all *non-empty* lines

Indented strings are primarily useful for allowing multi-line string literals to follow the indentation of the enclosing Nix expression, and easing escaping necessary for strings representing languages such as shell scripts and configuration files because `''` is much less common than `"`.
For example:
```nix
stdenv.mkDerivation {
    # other code would precede this
postInstall =
    ''
    mkdir $out/bin $out/etc
    cp foo $out/bin
    echo "Hello World" > $out/etc/foo.conf
    ${if enableBar then "cp bar $out/bin" else ""}
    '';
    # other code would follow this
}
```

Automatic indent stripping makes the following two strings are equivalent:
```nix
''
    This is the first line
    This is the second line
      This is the third line
''
```
```nix
"This is the first line\nThis is the second line\n  This is the third line\n"
```

#### Bare URIs

> *bare-uri* = [*uri*](http://www.ietf.org/rfc/rfc2396.txt)

As a convenience, *URIs* as defined in appendix B of
[RFC 2396](http://www.ietf.org/rfc/rfc2396.txt) can be written *as
is*, without quotes. For instance, the string
`"http://example.org/foo.tar.bz2"` can also be written as
`http://example.org/foo.tar.bz2`.

- <a id="type-number" href="#type-number">Number</a>

  Numbers, which can be *integers* (like `123`) or *floating point*
  (like `123.43` or `.27e13`).

  See [arithmetic] and [comparison] operators for semantics.

  [arithmetic]: ./operators.md#arithmetic
  [comparison]: ./operators.md#comparison

- <a id="type-path" href="#type-path">Path</a>

  *Paths*, e.g., `/bin/sh` or `./builder.sh`. A path must contain at
  least one slash to be recognised as such. For instance, `builder.sh`
  is not a path: it's parsed as an expression that selects the
  attribute `sh` from the variable `builder`. If the file name is
  relative, i.e., if it does not begin with a slash, it is made
  absolute at parse time relative to the directory of the Nix
  expression that contained it. For instance, if a Nix expression in
  `/foo/bar/bla.nix` refers to `../xyzzy/fnord.nix`, the absolute path
  is `/foo/xyzzy/fnord.nix`.

  If the first component of a path is a `~`, it is interpreted as if
  the rest of the path were relative to the user's home directory.
  e.g. `~/foo` would be equivalent to `/home/edolstra/foo` for a user
  whose home directory is `/home/edolstra`.

  For instance, evaluating `"${./foo.txt}"` will cause `foo.txt` in the current directory to be copied into the Nix store and result in the string `"/nix/store/<hash>-foo.txt"`.

  Note that the Nix language assumes that all input files will remain _unchanged_ while  evaluating a Nix expression.
  For example, assume you used a file path in an interpolated string during a `nix repl` session.
  Later in the same session, after having changed the file contents, evaluating the interpolated string with the file path again might not return a new [store path], since Nix might not re-read the file contents.

  [store path]: ../glossary.md#gloss-store-path

  Paths can include [string interpolation] and can themselves be [interpolated in other expressions].

  [interpolated in other expressions]: ./string-interpolation.md#interpolated-expressions

  At least one slash (`/`) must appear *before* any interpolated expression for the result to be recognized as a path.

  `a.${foo}/b.${bar}` is a syntactically valid division operation.
  `./a.${foo}/b.${bar}` is a path.

  [Lookup paths](./constructs/lookup-path.md) such as `<nixpkgs>` resolve to path values.

- <a id="type-boolean" href="#type-boolean">Boolean</a>

  *Booleans* with values `true` and `false`.

- <a id="type-null" href="#type-null">Null</a>

  The null value, denoted as `null`.

## List

Lists are formed by enclosing a whitespace-separated list of values
between square brackets. For example,

```nix
[ 123 ./foo.nix "abc" (f { x = y; }) ]
```

defines a list of four elements, the last being the result of a call to
the function `f`. Note that function calls have to be enclosed in
parentheses. If they had been omitted, e.g.,

```nix
[ 123 ./foo.nix "abc" f { x = y; } ]
```

the result would be a list of five elements, the fourth one being a
function and the fifth being a set.

Note that lists are only lazy in values, and they are strict in length.

## Attribute Set

An attribute set is a collection of name-value-pairs (called *attributes*) enclosed in curly brackets (`{ }`).

An attribute name can be an identifier or a [string](#string).
An identifier must start with a letter (`a-z`, `A-Z`) or underscore (`_`), and can otherwise contain letters (`a-z`, `A-Z`), numbers (`0-9`), underscores (`_`), apostrophes (`'`), or dashes (`-`).

> **Syntax**
>
> *name* = *identifier* | *string* \
> *identifier* ~ `[a-zA-Z_][a-zA-Z0-9_'-]*`

Names and values are separated by an equal sign (`=`).
Each value is an arbitrary expression terminated by a semicolon (`;`).

> **Syntax**
>
> *attrset* = `{` [ *name* `=` *expr* `;` ]... `}`

Attributes can appear in any order.
An attribute name may only occur once.

Example:

```nix
{
  x = 123;
  text = "Hello";
  y = f { bla = 456; };
}
```

This defines a set with attributes named `x`, `text`, `y`.

Attributes can be accessed with the [`.` operator](./operators.md#attribute-selection).

Example:

```nix
{ a = "Foo"; b = "Bar"; }.a
```

This evaluates to `"Foo"`.

It is possible to provide a default value in an attribute selection using the `or` keyword.

Example:

```nix
{ a = "Foo"; b = "Bar"; }.c or "Xyzzy"
```

```nix
{ a = "Foo"; b = "Bar"; }.c.d.e.f.g or "Xyzzy"
```

will both evaluate to `"Xyzzy"` because there is no `c` attribute in the set.

You can use arbitrary double-quoted strings as attribute names:

```nix
{ "$!@#?" = 123; }."$!@#?"
```

```nix
let bar = "bar"; in
{ "foo ${bar}" = 123; }."foo ${bar}"
```

Both will evaluate to `123`.

Attribute names support [string interpolation]:

```nix
let bar = "foo"; in
{ foo = 123; }.${bar}
```

```nix
let bar = "foo"; in
{ ${bar} = 123; }.foo
```

Both will evaluate to `123`.

In the special case where an attribute name inside of a set declaration
evaluates to `null` (which is normally an error, as `null` cannot be coerced to
a string), that attribute is simply not added to the set:

```nix
{ ${if foo then "bar" else null} = true; }
```

This will evaluate to `{}` if `foo` evaluates to `false`.

A set that has a `__functor` attribute whose value is callable (i.e. is
itself a function or a set with a `__functor` attribute whose value is
callable) can be applied as if it were a function, with the set itself
passed in first , e.g.,

```nix
let add = { __functor = self: x: x + self.x; };
    inc = add // { x = 1; };
in inc 1
```

evaluates to `2`. This can be used to attach metadata to a function
without the caller needing to treat it specially, or to implement a form
of object-oriented programming, for example.
