# Data Types

## Primitives

- <a id="type-string" href="#type-string">String</a>

  *Strings* can be written in three ways.

  The most common way is to enclose the string between double quotes,
  e.g., `"foo bar"`. Strings can span multiple lines. The special
  characters `"` and `\` and the character sequence `${` must be
  escaped by prefixing them with a backslash (`\`). Newlines, carriage
  returns and tabs can be written as `\n`, `\r` and `\t`,
  respectively.

  You can include the results of other expressions into a string by enclosing them in `${ }`, a feature known as [string interpolation].

  [string interpolation]: ./string-interpolation.md

  The second way to write string literals is as an *indented string*,
  which is enclosed between pairs of *double single-quotes*, like so:

  ```nix
  ''
    This is the first line.
    This is the second line.
      This is the third line.
  ''
  ```

  This kind of string literal intelligently strips indentation from
  the start of each line. To be precise, it strips from each line a
  number of spaces equal to the minimal indentation of the string as a
  whole (disregarding the indentation of empty lines). For instance,
  the first and second line are indented two spaces, while the third
  line is indented four spaces. Thus, two spaces are stripped from
  each line, so the resulting string is

  ```nix
  "This is the first line.\nThis is the second line.\n  This is the third line.\n"
  ```

  Note that the whitespace and newline following the opening `''` is
  ignored if there is no non-whitespace text on the initial line.

  Indented strings support [string interpolation].

  Since `${` and `''` have special meaning in indented strings, you
  need a way to quote them. `$` can be escaped by prefixing it with
  `''` (that is, two single quotes), i.e., `''$`. `''` can be escaped
  by prefixing it with `'`, i.e., `'''`. `$` removes any special
  meaning from the following `$`. Linefeed, carriage-return and tab
  characters can be written as `''\n`, `''\r`, `''\t`, and `''\`
  escapes any other character.

  Indented strings are primarily useful in that they allow multi-line
  string literals to follow the indentation of the enclosing Nix
  expression, and that less escaping is typically necessary for
  strings representing languages such as shell scripts and
  configuration files because `''` is much less common than `"`.
  Example:

  ```nix
  stdenv.mkDerivation {
    ...
    postInstall =
      ''
        mkdir $out/bin $out/etc
        cp foo $out/bin
        echo "Hello World" > $out/etc/foo.conf
        ${if enableBar then "cp bar $out/bin" else ""}
      '';
    ...
  }
  ```

  Finally, as a convenience, *URIs* as defined in appendix B of
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

  *Paths* are distinct from strings and can be expressed by path literals such as `./builder.sh`.

  Paths are suitable for referring to local files, and are often preferable over strings.
  - Path values do not contain trailing slashes, `.` and `..`, as they are resolved when evaluating a path literal.
  - Path literals are automatically resolved relative to their [base directory](@docroot@/glossary.md#gloss-base-directory).
  - The files referred to by path values are automatically copied into the Nix store when used in a string interpolation or concatenation.
  - Tooling can recognize path literals and provide additional features, such as autocompletion, refactoring automation and jump-to-file.

  A path literal must contain at least one slash to be recognised as such.
  For instance, `builder.sh` is not a path:
  it's parsed as an expression that selects the attribute `sh` from the variable `builder`.

  Path literals may also refer to absolute paths by starting with a slash.

  > **Note**
  >
  > Absolute paths make expressions less portable.
  > In the case where a function translates a path literal into an absolute path string for a configuration file, it is recommended to write a string literal instead.
  > This avoids some confusion about whether files at that location will be used during evaluation.
  > It also avoids unintentional situations where some function might try to copy everything at the location into the store.

  If the first component of a path is a `~`, it is interpreted such that the rest of the path were relative to the user's home directory.
  For example, `~/foo` would be equivalent to `/home/edolstra/foo` for a user whose home directory is `/home/edolstra`.
  Path literals that start with `~` are not allowed in [pure](@docroot@/command-ref/conf-file.md#conf-pure-eval) evaluation.

  Paths can be used in [string interpolation] and string concatenation.
  For instance, evaluating `"${./foo.txt}"` will cause `foo.txt` from the same directory to be copied into the Nix store and result in the string `"/nix/store/<hash>-foo.txt"`.

  Note that the Nix language assumes that all input files will remain _unchanged_ while evaluating a Nix expression.
  For example, assume you used a file path in an interpolated string during a `nix repl` session.
  Later in the same session, after having changed the file contents, evaluating the interpolated string with the file path again might not return a new [store path], since Nix might not re-read the file contents. Use `:r` to reset the repl as needed.

  [store path]: @docroot@/glossary.md#gloss-store-path

  Path literals can also include [string interpolation], besides being [interpolated into other expressions].

  [interpolated into other expressions]: ./string-interpolation.md#interpolated-expressions

  At least one slash (`/`) must appear *before* any interpolated expression for the result to be recognized as a path.

  `a.${foo}/b.${bar}` is a syntactically valid number division operation.
  `./a.${foo}/b.${bar}` is a path.

  [Lookup path](./constructs/lookup-path.md) literals such as `<nixpkgs>` also resolve to path values.

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

Elements in a list can be accessed using [`builtins.elemAt`](./builtins.md#builtins-elemAt). 

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
