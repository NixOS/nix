# String interpolation

String interpolation is a language feature where a [string], [path], or [attribute name][attribute set] can contain expressions enclosed in `${ }` (dollar-sign with curly brackets).

Such a construct is called *interpolated string*, and the expression inside is an [interpolated expression](#interpolated-expression).

[string]: ./types.md#type-string
[path]: ./types.md#type-path
[attribute set]: ./types.md#type-attrs

> **Syntax**
>
> *interpolation_element* → `${` *expression* `}`

## Examples

### String

Rather than writing

```nix
"--with-freetype2-library=" + freetype + "/lib"
```

(where `freetype` is a [derivation expression]), you can instead write

[derivation expression]: @docroot@/glossary.md#gloss-derivation-expression

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

To write a literal `${` in an regular string, escape it with a backslash (`\`).

> **Example**
>
> ```nix
> "echo \${PATH}"
> ```
>
>     "echo ${PATH}"

To write a literal `${` in an indented string, escape it with two single quotes (`''`).

> **Example**
>
> ```nix
> ''
>   echo ''${PATH}
> ''
> ```
>
>     "echo ${PATH}\n"

`$${` can be written literally in any string.

> **Example**
>
> In Make, `$` in file names or recipes is represented as `$$`, see [GNU `make`: Basics of Variable Reference](https://www.gnu.org/software/make/manual/html_node/Reference.html#Basics-of-Variable-References).
> This can be expressed directly in the Nix language strings:
>
> ```nix
> ''
>   MAKEVAR = Hello
>   all:
>   	@export BASHVAR=world; echo $(MAKEVAR) $${BASHVAR}
> ''
> ```
>
>     "MAKEVAR = Hello\nall:\n\t@export BASHVAR=world; echo $(MAKEVAR) $\${BASHVAR}\n"

See the [documentation on strings][string] for details.

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

<!--
FIXME: these examples are redundant with the main page on attribute sets.
figure out what to do about that
-->

Attribute names can be interpolated strings.

> **Example**
>
> ```nix
> let name = "foo"; in
> { ${name} = 123; }
> ```
>
>     { foo = 123; }

Attributes can be selected with interpolated strings.

> **Example**
>
> ```nix
> let name = "foo"; in
> { foo = 123; }.${name}
> ```
>
>     123

# Interpolated expression

An expression that is interpolated must evaluate to one of the following:

- a [string]
- a [path]
- an [attribute set] that has a `__toString` attribute or an `outPath` attribute

  - `__toString` must be a function that takes the attribute set itself and returns a string
  - `outPath` must be a string

  This includes [derivation expressions](./derivations.md) or [flake inputs](@docroot@/command-ref/new-cli/nix3-flake.md#flake-inputs) (experimental).

A string interpolates to itself.

A path in an interpolated expression is first copied into the Nix store, and the resulting string is the [store path] of the newly created [store object](@docroot@/store/store-object.md).

[store path]: @docroot@/store/store-path.md

> **Example**
>
> ```console
> $ mkdir foo
> ```
>
> Reference the empty directory in an interpolated expression:
>
> ```nix
> "${./foo}"
> ```
>
>     "/nix/store/2hhl2nz5v0khbn06ys82nrk99aa1xxdw-foo"

A derivation interpolates to the [store path] of its first [output](./derivations.md#attr-outputs).

> **Example**
>
> ```nix
> let
>   pkgs = import <nixpkgs> {};
> in
> "${pkgs.hello}"
> ```
>
>     "/nix/store/4xpfqf29z4m8vbhrqcz064wfmb46w5r7-hello-2.12.1"

An attribute set interpolates to the return value of the function in the `__toString` applied to the attribute set itself.

> **Example**
>
> ```nix
> let
>   a = {
>     value = 1;
>     __toString = self: toString (self.value + 1);
>   };
> in
> "${a}"
> ```
>
>     "2"

An attribute set also interpolates to the value of its `outPath` attribute.

> **Example**
>
> ```nix
> let
>   a = { outPath = "foo"; };
> in
> "${a}"
> ```
>
>     "foo"

If both `__toString` and `outPath` are present in an attribute set, `__toString` takes precedence.

> **Example**
>
> ```nix
> let
>   a = { __toString = _: "yes"; outPath = throw "no"; };
> in
> "${a}"
> ```
>
>     "yes"

If neither is present, an error is thrown.

> **Example**
>
> ```nix
> let
>   a = {};
> in
> "${a}"
> ```
>
>     error: cannot coerce a set to a string: { }
>
>            at «string»:4:2:
>
>                 3| in
>                 4| "${a}"
>                  |  ^
