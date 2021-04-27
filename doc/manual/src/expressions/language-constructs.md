# Language Constructs

## Recursive sets

Recursive sets are just normal sets, but the attributes can refer to
each other. For example,

```nix
rec {
  x = y;
  y = 123;
}.x
```

evaluates to `123`. Note that without `rec` the binding `x = y;` would
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
For instance,

```nix
let
  x = "foo";
  y = "bar";
in x + y
```

evaluates to `"foobar"`.

## Inheriting attributes

When defining a set or in a let-expression it is often convenient to
copy variables from the surrounding lexical scope (e.g., when you want
to propagate attributes). This can be shortened using the `inherit`
keyword. For instance,

```nix
let x = 123; in
{ inherit x;
  y = 456;
}
```

is equivalent to

```nix
let x = 123; in
{ x = x;
  y = 456;
}
```

and both evaluate to `{ x = 123; y = 456; }`. (Note that this works
because `x` is added to the lexical scope by the `let` construct.) It is
also possible to inherit attributes from another set. For instance, in
this fragment from `all-packages.nix`,

```nix
graphviz = (import ../tools/graphics/graphviz) {
  inherit fetchurl stdenv libpng libjpeg expat x11 yacc;
  inherit (xlibs) libXaw;
};

xlibs = {
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
(the X Athena Widgets) from the `xlibs` (X11 client-side libraries) set.

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
    
    It is possible to provide *default values* for attributes, in
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
    
    Here `args` is bound to the entire argument, which is further
    matched against the pattern `{ x, y, z,
            ... }`. `@`-pattern makes mainly sense with an ellipsis(`...`) as
    you can access attribute names as `a`, using `args.a`, which was
    given as an additional attribute to the function.
    
    > **Warning**
    > 
    > The `args@` expression is bound to the argument passed to the
    > function which means that attributes with defaults that aren't
    > explicitly specified in the function call won't cause an
    > evaluation error, but won't exist in `args`.
    > 
    > For instance
    > 
    > ```nix
    > let
    >   function = args@{ a ? 23, ... }: args;
    > in
    >   function {}
    > ````
    > 
    > will evaluate to an empty attribute set.

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

## Comments

Comments can be single-line, started with a `#` character, or
inline/multi-line, enclosed within `/* ... */`.
