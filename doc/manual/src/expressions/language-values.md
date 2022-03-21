# Values

## Simple Values

Nix has the following basic data types:

  - *Strings* can be written in three ways.
    
    The most common way is to enclose the string between double quotes,
    e.g., `"foo bar"`. Strings can span multiple lines. The special
    characters `"` and `\` and the character sequence `${` must be
    escaped by prefixing them with a backslash (`\`). Newlines, carriage
    returns and tabs can be written as `\n`, `\r` and `\t`,
    respectively.
    
    You can include the result of an expression into a string by
    enclosing it in `${...}`, a feature known as *antiquotation*. The
    enclosed expression must evaluate to something that can be coerced
    into a string (meaning that it must be a string, a path, or a
    derivation). For instance, rather than writing
    
    ```nix
    "--with-freetype2-library=" + freetype + "/lib"
    ```
    
    (where `freetype` is a derivation), you can instead write the more
    natural
    
    ```nix
    "--with-freetype2-library=${freetype}/lib"
    ```
    
    The latter is automatically translated to the former. A more
    complicated example (from the Nix expression for
    [Qt](http://www.trolltech.com/products/qt)):
    
    ```nix
    configureFlags = "
      -system-zlib -system-libpng -system-libjpeg
      ${if openglSupport then "-dlopen-opengl
        -L${mesa}/lib -I${mesa}/include
        -L${libXmu}/lib -I${libXmu}/include" else ""}
      ${if threadSupport then "-thread" else "-no-thread"}
    ";
    ```
    
    Note that Nix expressions and strings can be arbitrarily nested; in
    this case the outer string contains various antiquotations that
    themselves contain strings (e.g., `"-thread"`), some of which in
    turn contain expressions (e.g., `${mesa}`).
    
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
    
    Antiquotation (`${expr}`) is supported in indented strings.
    
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

  - Numbers, which can be *integers* (like `123`) or *floating point*
    (like `123.43` or `.27e13`).
    
    Numbers are type-compatible: pure integer operations will always
    return integers, whereas any operation involving at least one
    floating point number will have a floating point number as a result.

  - *Paths*, e.g., `/bin/sh` or `./builder.sh`. A path must contain at
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
    
    Paths can also be specified between angle brackets, e.g.
    `<nixpkgs>`. This means that the directories listed in the
    environment variable `NIX_PATH` will be searched for the given file
    or directory name.

    Antiquotation is supported in any paths except those in angle brackets.
    `./${foo}-${bar}.nix` is a more convenient way of writing 
    `./. + "/" + foo + "-" + bar + ".nix"` or `./. + "/${foo}-${bar}.nix"`. At
    least one slash must appear *before* any antiquotations for this to be
    recognized as a path. `a.${foo}/b.${bar}` is a syntactically valid division
    operation. `./a.${foo}/b.${bar}` is a path.

  - *Booleans* with values `true` and `false`.

  - The null value, denoted as `null`.

## Lists

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

## Sets

Sets are really the core of the language, since ultimately the Nix
language is all about creating derivations, which are really just sets
of attributes to be passed to build scripts.

Sets are just a list of name/value pairs (called *attributes*) enclosed
in curly brackets, where each value is an arbitrary expression
terminated by a semicolon. For example:

```nix
{ x = 123;
  text = "Hello";
  y = f { bla = 456; };
}
```

This defines a set with attributes named `x`, `text`, `y`. The order of
the attributes is irrelevant. An attribute name may only occur once.

Attributes can be selected from a set using the `.` operator. For
instance,

```nix
{ a = "Foo"; b = "Bar"; }.a
```

evaluates to `"Foo"`. It is possible to provide a default value in an
attribute selection using the `or` keyword. For example,

```nix
{ a = "Foo"; b = "Bar"; }.c or "Xyzzy"
```

will evaluate to `"Xyzzy"` because there is no `c` attribute in the set.

You can use arbitrary double-quoted strings as attribute names:

```nix
{ "foo ${bar}" = 123; "nix-1.0" = 456; }."foo ${bar}"
```

This will evaluate to `123` (Assuming `bar` is antiquotable). In the
case where an attribute name is just a single antiquotation, the quotes
can be dropped:

```nix
{ foo = 123; }.${bar} or 456
```

This will evaluate to `123` if `bar` evaluates to `"foo"` when coerced
to a string and `456` otherwise (again assuming `bar` is antiquotable).

In the special case where an attribute name inside of a set declaration
evaluates to `null` (which is normally an error, as `null` is not
antiquotable), that attribute is simply not added to the set:

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
