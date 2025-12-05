# Language Constructs

This section covers syntax and semantics of the Nix language.

## Basic Literals

### String {#string-literal}

See [String literals](string-literals.md).

### Number {#number-literal}

<!-- TODO(@rhendric, #10970): split this into int and float -->

  Numbers, which can be *integers* (like `123`) or *floating point*
  (like `123.43` or `.27e13`).

  Integers in the Nix language are 64-bit [two's complement] signed integers, with a range of -9223372036854775808 to 9223372036854775807, inclusive.

  [two's complement]: https://en.wikipedia.org/wiki/Two%27s_complement

  Note that negative numeric literals are actually parsed as unary negation of positive numeric literals.
  This means that the minimum integer `-9223372036854775808` cannot be written as-is as a literal, since the positive number `9223372036854775808` is one past the maximum range.

  See [arithmetic] and [comparison] operators for semantics.

  [arithmetic]: ./operators.md#arithmetic
  [comparison]: ./operators.md#comparison

### Path {#path-literal}

  *Paths* can be expressed by path literals such as `./builder.sh`.

  A path literal must contain at least one slash to be recognised as such.
  For instance, `builder.sh` is not a path:
  it's parsed as an expression that selects the attribute `sh` from the variable `builder`.

  Path literals are resolved relative to their [base directory](@docroot@/glossary.md#gloss-base-directory).
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

  Path literals can also include [string interpolation], besides being [interpolated into other expressions].

  [interpolated into other expressions]: ./string-interpolation.md#interpolated-expression

  At least one slash (`/`) must appear *before* any interpolated expression for the result to be recognized as a path.

  `a.${foo}/b.${bar}` is a syntactically valid number division operation.
  `./a.${foo}/b.${bar}` is a path.

  [Lookup path](./constructs/lookup-path.md) literals such as `<nixpkgs>` also resolve to path values.

## List {#list-literal}

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

## Attribute Set {#attrs-literal}

An attribute set is a collection of name-value-pairs called *attributes*.

Attribute sets are written enclosed in curly brackets (`{ }`).
Attribute names and attribute values are separated by an equal sign (`=`).
Each value can be an arbitrary expression, terminated by a semicolon (`;`)

An attribute name is a string without context, and is denoted by a [name] (an [identifier](./identifiers.md#identifiers) or [string literal](string-literals.md)).

[name]: ./identifiers.md#names

> **Syntax**
>
> *attrset* → `{` { *name* `=` *expr* `;` } `}`

Attributes can appear in any order.
An attribute name may only occur once in each attribute set.

> **Example**
>
> This defines an attribute set with attributes named:
> - `x` with the value `123`, an integer
> - `text` with the value `"Hello"`, a string
> - `y` where the value is the result of applying the function `f` to the attribute set `{ bla = 456; }`
>
> ```nix
> {
>   x = 123;
>   text = "Hello";
>   y = f { bla = 456; };
> }
> ```

Attributes in nested attribute sets can be written using *attribute paths*.

> **Syntax**
>
> *attrset* → `{` { *attrpath* `=` *expr* `;` } `}`

An attribute path is a dot-separated list of [names][name].

> **Syntax**
>
> *attrpath* = *name* { `.` *name* }

<!-- -->

> **Example**
>
> ```nix
> { a.b.c = 1; a.b.d = 2; }
> ```
>
>     {
>       a = {
>         b = {
>           c = 1;
>           d = 2;
>         };
>       };
>     }

Attribute names can also be set implicitly by using the [`inherit` keyword](#inheriting-attributes).

> **Example**
>
> ```nix
> { inherit (builtins) true; }
> ```
>
>     { true = true; }

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

A set that has a [`__functor`]{#attr-__functor} attribute whose value is callable (i.e. is
itself a function or a set with a `__functor` attribute whose value is
callable) can be applied as if it were a function, with the set itself
passed in first , e.g.,

```nix
let add = { __functor = self: x: x + self.x; };
    inc = add // { x = 1; }; # inc is { x = 1; __functor = (...) }
in inc 1 # equivalent of `add.__functor add 1` i.e. `1 + self.x`
```

evaluates to `2`. This can be used to attach metadata to a function
without the caller needing to treat it specially, or to implement a form
of object-oriented programming, for example.

## Recursive sets

Recursive sets are like normal [attribute sets](./types.md#type-attrs), but the attributes can refer to each other.

> *rec-attrset* = `rec {` [ *name* `=` *expr* `;` `]`... `}`

Example:

```nix
rec {
  x = y;
  y = 123;
}.x
```

This evaluates to `123`.

Note that without `rec` the binding `x = y;` would
refer to the variable `y` in the surrounding scope, if one exists, and
would be invalid if no such variable exists. That is, in a normal
(non-recursive) set, attributes are not added to the lexical scope; in a
recursive set, they are.

Recursive sets of course introduce the danger of infinite recursion. For
example, the expression

```nix
rec {
  x = y;
  y = x;
}.x
```

will crash with an `infinite recursion encountered` error message.

## Let-expressions

A let-expression allows you to define local variables for an expression.

> *let-in* = `let` [ *identifier* = *expr* ]... `in` *expr*

Example:

```nix
let
  x = "foo";
  y = "bar";
in x + y
```

This evaluates to `"foobar"`.

## Inheriting attributes

When defining an [attribute set](./types.md#type-attrs) or in a [let-expression](#let-expressions) it is often convenient to copy variables from the surrounding lexical scope (e.g., when you want to propagate attributes).
This can be shortened using the `inherit` keyword.

Example:

```nix
let x = 123; in
{
  inherit x;
  y = 456;
}
```

is equivalent to

```nix
let x = 123; in
{
  x = x;
  y = 456;
}
```

and both evaluate to `{ x = 123; y = 456; }`.

> **Note**
>
> This works because `x` is added to the lexical scope by the `let` construct.

It is also possible to inherit attributes from another attribute set.

Example:

In this fragment from `all-packages.nix`,

```nix
graphviz = (import ../tools/graphics/graphviz) {
  inherit fetchurl stdenv libpng libjpeg expat x11 yacc;
  inherit (xorg) libXaw;
};

xorg = {
  libX11 = ...;
  libXaw = ...;
  ...
}

libpng = ...;
libjpg = ...;
...
```

the set used in the function call to the function defined in
`../tools/graphics/graphviz` inherits a number of variables from the
surrounding scope (`fetchurl` ... `yacc`), but also inherits `libXaw`
(the X Athena Widgets) from the `xorg` set.

Summarizing the fragment

```nix
...
inherit x y z;
inherit (src-set) a b c;
...
```

is equivalent to

```nix
...
x = x; y = y; z = z;
a = src-set.a; b = src-set.b; c = src-set.c;
...
```

when used while defining local variables in a let-expression or while
defining a set.

In a `let` expression, `inherit` can be used to selectively bring specific attributes of a set into scope. For example


```nix
let
  x = { a = 1; b = 2; };
  inherit (builtins) attrNames;
in
{
  names = attrNames x;
}
```

is equivalent to

```nix
let
  x = { a = 1; b = 2; };
in
{
  names = builtins.attrNames x;
}
```

both evaluate to `{ names = [ "a" "b" ]; }`.

## Functions

Functions have the following form:

```nix
pattern: body
```

The pattern specifies what the argument of the function must look like,
and binds variables in the body to (parts of) the argument. There are
three kinds of patterns:

  - If a pattern is a single identifier, then the function matches any
    argument. Example:

    ```nix
    let negate = x: !x;
        concat = x: y: x + y;
    in if negate true then concat "foo" "bar" else ""
    ```

    Note that `concat` is a function that takes one argument and returns
    a function that takes another argument. This allows partial
    parameterisation (i.e., only filling some of the arguments of a
    function); e.g.,

    ```nix
    map (concat "foo") [ "bar" "bla" "abc" ]
    ```

    evaluates to `[ "foobar" "foobla" "fooabc" ]`.

  - A *set pattern* of the form `{ name1, name2, …, nameN }` matches a
    set containing the listed attributes, and binds the values of those
    attributes to variables in the function body. For example, the
    function

    ```nix
    { x, y, z }: z + y + x
    ```

    can only be called with a set containing exactly the attributes `x`,
    `y` and `z`. No other attributes are allowed. If you want to allow
    additional arguments, you can use an ellipsis (`...`):

    ```nix
    { x, y, z, ... }: z + y + x
    ```

    This works on any set that contains at least the three named
    attributes.

  - It is possible to provide *default values* for attributes, in
    which case they are allowed to be missing. A default value is
    specified by writing `name ?  e`, where *e* is an arbitrary
    expression. For example,

    ```nix
    { x, y ? "foo", z ? "bar" }: z + y + x
    ```

    specifies a function that only requires an attribute named `x`, but
    optionally accepts `y` and `z`.

  - An `@`-pattern provides a means of referring to the whole value
    being matched:

    ```nix
    args@{ x, y, z, ... }: z + y + x + args.a
    ```

    but can also be written as:

    ```nix
    { x, y, z, ... } @ args: z + y + x + args.a
    ```

    Here `args` is bound to the argument *as passed*, which is further
    matched against the pattern `{ x, y, z, ... }`.
    The `@`-pattern makes mainly sense with an ellipsis(`...`) as
    you can access attribute names as `a`, using `args.a`, which was
    given as an additional attribute to the function.

    > **Warning**
    >
    > `args@` binds the name `args` to the attribute set that is passed to the function.
    > In particular, `args` does *not* include any default values specified with `?` in the function's set pattern.
    >
    > For instance
    >
    > ```nix
    > let
    >   f = args@{ a ? 23, ... }: [ a args ];
    > in
    >   f {}
    > ```
    >
    > is equivalent to
    >
    > ```nix
    > let
    >   f = args @ { ... }: [ (args.a or 23) args ];
    > in
    >   f {}
    > ```
    >
    > and both expressions will evaluate to:
    >
    > ```nix
    > [ 23 {} ]
    > ```

  - All bindings introduced by the function are in scope in the entire function expression; not just in the body.
    It can therefore be used in default values.

    > **Example**
    >
    > A parameter (`x`), is used in the default value for another parameter (`y`):
    >
    > ```nix
    > let
    >   f = { x, y ? [x] }: { inherit y; };
    > in
    >   f { x = 3; }
    > ```
    >
    > This evaluates to:
    >
    > ```nix
    > {
    >   y = [ 3 ];
    > }
    > ```

    > **Example**
    >
    > The binding of an `@` pattern, `args`, is used in the default value for a parameter, `x`:
    >
    > ```nix
    > let
    >   f = args@{ x ? args.a, ... }: x;
    > in
    >   f { a = 1; }
    > ```
    >
    > This evaluates to:
    >
    > ```nix
    > 1
    > ```

Note that functions do not have names. If you want to give them a name,
you can bind them to an attribute, e.g.,

```nix
let concat = { x, y }: x + y;
in concat { x = "foo"; y = "bar"; }
```

## Conditionals

Conditionals look like this:

```nix
if e1 then e2 else e3
```

where *e1* is an expression that should evaluate to a Boolean value
(`true` or `false`).

## Assertions

Assertions are generally used to check that certain requirements on or
between features and dependencies hold. They look like this:

```nix
assert e1; e2
```

where *e1* is an expression that should evaluate to a Boolean value. If
it evaluates to `true`, *e2* is returned; otherwise expression
evaluation is aborted and a backtrace is printed.

Here is a Nix expression for the Subversion package that shows how
assertions can be used:.

```nix
{ localServer ? false
, httpServer ? false
, sslSupport ? false
, pythonBindings ? false
, javaSwigBindings ? false
, javahlBindings ? false
, stdenv, fetchurl
, openssl ? null, httpd ? null, db4 ? null, expat, swig ? null, j2sdk ? null
}:

assert localServer -> db4 != null; ①
assert httpServer -> httpd != null && httpd.expat == expat; ②
assert sslSupport -> openssl != null && (httpServer -> httpd.openssl == openssl); ③
assert pythonBindings -> swig != null && swig.pythonSupport;
assert javaSwigBindings -> swig != null && swig.javaSupport;
assert javahlBindings -> j2sdk != null;

stdenv.mkDerivation {
  name = "subversion-1.1.1";
  ...
  openssl = if sslSupport then openssl else null; ④
  ...
}
```

The points of interest are:

1.  This assertion states that if Subversion is to have support for
    local repositories, then Berkeley DB is needed. So if the Subversion
    function is called with the `localServer` argument set to `true` but
    the `db4` argument set to `null`, then the evaluation fails.

    Note that `->` is the [logical
    implication](https://en.wikipedia.org/wiki/Truth_table#Logical_implication)
    Boolean operation.

2.  This is a more subtle condition: if Subversion is built with Apache
    (`httpServer`) support, then the Expat library (an XML library) used
    by Subversion should be same as the one used by Apache. This is
    because in this configuration Subversion code ends up being linked
    with Apache code, and if the Expat libraries do not match, a build-
    or runtime link error or incompatibility might occur.

3.  This assertion says that in order for Subversion to have SSL support
    (so that it can access `https` URLs), an OpenSSL library must be
    passed. Additionally, it says that *if* Apache support is enabled,
    then Apache's OpenSSL should match Subversion's. (Note that if
    Apache support is not enabled, we don't care about Apache's
    OpenSSL.)

4.  The conditional here is not really related to assertions, but is
    worth pointing out: it ensures that if SSL support is disabled, then
    the Subversion derivation is not dependent on OpenSSL, even if a
    non-`null` value was passed. This prevents an unnecessary rebuild of
    Subversion if OpenSSL changes.

## With-expressions

A *with-expression*,

```nix
with e1; e2
```

introduces the set *e1* into the lexical scope of the expression *e2*.
For instance,

```nix
let as = { x = "foo"; y = "bar"; };
in with as; x + y
```

evaluates to `"foobar"` since the `with` adds the `x` and `y` attributes
of `as` to the lexical scope in the expression `x + y`. The most common
use of `with` is in conjunction with the `import` function. E.g.,

```nix
with (import ./definitions.nix); ...
```

makes all attributes defined in the file `definitions.nix` available as
if they were defined locally in a `let`-expression.

The bindings introduced by `with` do not shadow bindings introduced by
other means, e.g.

```nix
let a = 3; in with { a = 1; }; let a = 4; in with { a = 2; }; ...
```

establishes the same scope as

```nix
let a = 1; in let a = 2; in let a = 3; in let a = 4; in ...
```

Variables coming from outer `with` expressions *are* shadowed:

```nix
with { a = "outer"; };
with { a = "inner"; };
a
```

Does evaluate to `"inner"`.

## Comments

- Inline comments start with `#` and run until the end of the line.

  > **Example**
  >
  > ```nix
  > # A number
  > 2 # Equals 1 + 1
  > ```
  >
  > ```console
  > 2
  > ```

- Block comments start with `/*` and run until the next occurrence of `*/`.

  > **Example**
  >
  > ```nix
  > /*
  > Block comments
  > can span multiple lines.
  > */ "hello"
  > ```
  >
  > ```console
  > "hello"
  > ```

  This means that block comments cannot be nested.

  > **Example**
  >
  > ```nix
  > /* /* nope */ */ 1
  > ```
  >
  > ```console
  > error: syntax error, unexpected '*'
  >
  >        at «string»:1:15:
  >
  >             1| /* /* nope */ *
  >              |               ^
  > ```

  Consider escaping nested comments and unescaping them in post-processing.

  > **Example**
  >
  > ```nix
  > /* /* nested *\/ */ 1
  > ```
  >
  > ```console
  > 1
  > ```
