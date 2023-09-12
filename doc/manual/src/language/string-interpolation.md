# String interpolation

String interpolation is a language feature where a [string], [path], or [attribute name] can contain expressions enclosed in `${ }` (dollar-sign with curly brackets).

Such a string is an *interpolated string*, and an expression inside is an *interpolated expression*.

Interpolated expressions must evaluate to one of the following:

- a [string]
- a [path]
- a [derivation]

[string]: ./values.md#type-string
[path]: ./values.md#type-path
[attribute name]: ./values.md#attribute-set
[derivation]: ../glossary.md#gloss-derivation

## Examples

### String

Rather than writing

```nix
"--with-freetype2-library=" + freetype + "/lib"
```

(where `freetype` is a [derivation]), you can instead write

```nix
"--with-freetype2-library=${freetype}/lib"
```

The latter is automatically translated to the former.

A more complicated example (from the Nix expression for [Qt](http://www.trolltech.com/products/qt)):

```nix
configureFlags = "
  -system-zlib -system-libpng -system-libjpeg
  ${if openglSupport then "-dlopen-opengl
    -L${mesa}/lib -I${mesa}/include
    -L${libXmu}/lib -I${libXmu}/include" else ""}
  ${if threadSupport then "-thread" else "-no-thread"}
";
```

Note that Nix expressions and strings can be arbitrarily nested;
in this case the outer string contains various interpolated expressions that themselves contain strings (e.g., `"-thread"`), some of which in turn contain interpolated expressions (e.g., `${mesa}`).

### Path

Rather than writing

```nix
./. + "/" + foo + "-" + bar + ".nix"
```

or

```nix
./. + "/${foo}-${bar}.nix"
```

you can instead write

```nix
./${foo}-${bar}.nix
```

### Attribute name

Attribute names can be created dynamically with string interpolation:

```nix
let name = "foo"; in
{
  ${name} = "bar";
}
```

    { foo = "bar"; }
