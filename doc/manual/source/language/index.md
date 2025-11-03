# Nix Language

The Nix language is designed for conveniently creating and composing [derivations](@docroot@/glossary.md#gloss-derivation) – precise descriptions of how contents of existing files are used to derive new files.

> **Tip**
>
> These pages are written as a reference.
> If you are learning Nix, nix.dev has a good [introduction to the Nix language](https://nix.dev/tutorials/nix-language).

The language is:

- *domain-specific*

  The Nix language is purpose-built for working with text files.
  Its most characteristic features are:

  - [File system path primitives](@docroot@/language/types.md#type-path), for accessing source files
  - [Indented strings](@docroot@/language/string-literals.md) and [string interpolation](@docroot@/language/string-interpolation.md), for creating file contents
  - [Strings with contexts](@docroot@/language/string-context.md), for transparently linking files

  It comes with [built-in functions](@docroot@/language/builtins.md) to integrate with the [Nix store](@docroot@/store/index.md), which manages files and enables [realising](@docroot@/glossary.md#gloss-realise) derivations declared in the Nix language.

- *declarative*

  There is no notion of executing sequential steps.
  Dependencies between operations are established only through data.

- *pure*

  Values cannot change during computation.
  Functions always produce the same output if their input does not change.

- *functional*

  Functions are like any other value.
  Functions can be assigned to names, taken as arguments, or returned by functions.

- *lazy*

  Values are only computed when they are needed.

- *dynamically typed*

  Type errors are only detected when expressions are evaluated.

# Overview

This is an incomplete overview of language features, by example.

<table>
 <tr>
  <th>
   Example
  </th>
  <th>
   Description
  </th>
 </tr>
 <tr>
  <td>


   *Basic values ([primitives](@docroot@/language/types.md#primitives))*


  </td>
  <td>



  </td>
 </tr>
 <tr>
  <td>

   `"hello world"`

  </td>
  <td>

   A [string](@docroot@/language/types.md#type-string)

  </td>
 </tr>
 <tr>
  <td>

   ```
   ''
     multi
      line
       string
   ''
   ```

  </td>
  <td>

   <!-- FIXME: using two no-break spaces, because apparently mdBook swallows the second regular space! -->
   A multi-line string. Strips common prefixed whitespace. Evaluates to `"multi\n line\n  string"`.

  </td>
 </tr>
 <tr>
  <td>

   `# Explanation`

  </td>
  <td>

   A [comment](@docroot@/language/syntax.md#comments).

  </td>
 </tr>
 <tr>
  <td>

   `"hello ${ { a = "world"; }.a }"`

   `"1 2 ${toString 3}"`

   `"${pkgs.bash}/bin/sh"`

  </td>
  <td>

   [String interpolation](@docroot@/language/string-interpolation.md) (expands to `"hello world"`, `"1 2 3"`, `"/nix/store/<hash>-bash-<version>/bin/sh"`)

  </td>
 </tr>
 <tr>
  <td>

   `true`, `false`

  </td>
  <td>

   [Booleans](@docroot@/language/types.md#type-bool)

  </td>
 </tr>
 <tr>
  <td>

   `null`

  </td>
  <td>

   [Null](@docroot@/language/types.md#type-null) value

  </td>
 </tr>
 <tr>
  <td>

   `123`

  </td>
  <td>

   An [integer](@docroot@/language/types.md#type-int)

  </td>
 </tr>
 <tr>
  <td>

   `3.141`

  </td>
  <td>

   A [floating point number](@docroot@/language/types.md#type-float)

  </td>
 </tr>
 <tr>
  <td>

   `/etc`

  </td>
  <td>

   An absolute [path](@docroot@/language/types.md#type-path)

  </td>
 </tr>
 <tr>
  <td>

   `./foo.png`

  </td>
  <td>

   A [path](@docroot@/language/types.md#type-path) relative to the file containing this Nix expression

  </td>
 </tr>
 <tr>
  <td>

   `~/.config`

  </td>
  <td>

   A home [path](@docroot@/language/types.md#type-path). Evaluates to the `"<user's home directory>/.config"`.

  </td>
 </tr>
 <tr>
  <td>

   `<nixpkgs>`

  </td>
  <td>

   A [lookup path](@docroot@/language/constructs/lookup-path.md) for Nix files. Value determined by [`$NIX_PATH` environment variable](../command-ref/env-common.md#env-NIX_PATH).

  </td>
 </tr>
 <tr>
  <td>

   *Compound values*

  </td>
  <td>



  </td>
 </tr>
 <tr>
  <td>

   `{ x = 1; y = 2; }`

  </td>
  <td>

   An [attribute set](@docroot@/language/types.md#type-attrs) with attributes named `x` and `y`

  </td>
 </tr>
 <tr>
  <td>

   `{ foo.bar = 1; }`

  </td>
  <td>

   A nested set, equivalent to `{ foo = { bar = 1; }; }`

  </td>
 </tr>
 <tr>
  <td>

   `rec { x = "foo"; y = x + "bar"; }`

  </td>
  <td>

   A [recursive set](@docroot@/language/syntax.md#recursive-sets), equivalent to `{ x = "foo"; y = "foobar"; }`.

  </td>
 </tr>
 <tr>
  <td>

   `[ "foo" "bar" "baz" ]`

   `[ 1 2 3 ]`

   `[ (f 1) { a = 1; b = 2; } [ "c" ] ]`

  </td>
  <td>

   [Lists](@docroot@/language/types.md#type-list) with three elements.

  </td>
 </tr>
 <tr>
  <td>

   *Operators*

  </td>
  <td>



  </td>
 </tr>
 <tr>
  <td>

   `"foo" + "bar"`

  </td>
  <td>

   String concatenation

  </td>
 </tr>
 <tr>
  <td>

   `1 + 2`

  </td>
  <td>

   Integer addition

  </td>
 </tr>
 <tr>
  <td>

   `"foo" == "f" + "oo"`

  </td>
  <td>

   Equality test (evaluates to `true`)

  </td>
 </tr>
 <tr>
  <td>

   `"foo" != "bar"`

  </td>
  <td>

   Inequality test (evaluates to `true`)

  </td>
 </tr>
 <tr>
  <td>

   `!true`

  </td>
  <td>

   Boolean negation

  </td>
 </tr>
 <tr>
  <td>

   `{ x = 1; y = 2; }.x`

  </td>
  <td>

   [Attribute selection](@docroot@/language/types.md#type-attrs) (evaluates to `1`)

  </td>
 </tr>
 <tr>
  <td>

   `{ x = 1; y = 2; }.z or 3`

  </td>
  <td>

   [Attribute selection](@docroot@/language/types.md#type-attrs) with default (evaluates to `3`)

  </td>
 </tr>
 <tr>
  <td>

   `{ x = 1; y = 2; } // { z = 3; }`

  </td>
  <td>

   Merge two sets (attributes in the right-hand set taking precedence)

  </td>
 </tr>
 <tr>
  <td>

   *Control structures*

  </td>
  <td>



  </td>
 </tr>
 <tr>
  <td>

   `if 1 + 1 == 2 then "yes!" else "no!"`

  </td>
  <td>

   [Conditional expression](@docroot@/language/syntax.md#conditionals).

  </td>
 </tr>
 <tr>
  <td>

   `assert 1 + 1 == 2; "yes!"`

  </td>
  <td>

   [Assertion](@docroot@/language/syntax.md#assertions) check (evaluates to `"yes!"`).

  </td>
 </tr>
 <tr>
  <td>

   `let x = "foo"; y = "bar"; in x + y`

  </td>
  <td>

   Variable definition. See [`let`-expressions](@docroot@/language/syntax.md#let-expressions).

  </td>
 </tr>
 <tr>
  <td>

   `with builtins; head [ 1 2 3 ]`

  </td>
  <td>

   Add all attributes from the given set to the scope (evaluates to `1`).

   See [`with`-expressions](@docroot@/language/syntax.md#with-expressions) for details and shadowing caveats.

  </td>
 </tr>
 <tr>
  <td>

   `inherit pkgs src;`

  </td>
  <td>

   Adds the variables to the current scope (attribute set or `let` binding).
   Desugars to `pkgs = pkgs; src = src;`.
   See [Inheriting attributes](@docroot@/language/syntax.md#inheriting-attributes).

  </td>
 </tr>
 <tr>
  <td>

   `inherit (pkgs) lib stdenv;`

  </td>
  <td>

   Adds the attributes, from the attribute set in parentheses, to the current scope (attribute set or `let` binding).
   Desugars to `lib = pkgs.lib; stdenv = pkgs.stdenv;`.
   See [Inheriting attributes](@docroot@/language/syntax.md#inheriting-attributes).

  </td>
 </tr>
 <tr>
  <td>

   *[Functions](@docroot@/language/syntax.md#functions) (lambdas)*

  </td>
  <td>



  </td>
 </tr>
 <tr>
  <td>

   `x: x + 1`

  </td>
  <td>

   A [function](@docroot@/language/syntax.md#functions) that expects an integer and returns it increased by 1.

  </td>
 </tr>
 <tr>
  <td>

   `x: y: x + y`

  </td>
  <td>

   Curried [function](@docroot@/language/syntax.md#functions), equivalent to `x: (y: x + y)`. Can be used like a function that takes two arguments and returns their sum.

  </td>
 </tr>
 <tr>
  <td>

   `(x: x + 1) 100`

  </td>
  <td>

   A [function](@docroot@/language/syntax.md#functions) call (evaluates to 101)

  </td>
 </tr>
 <tr>
  <td>

   `let inc = x: x + 1; in inc (inc (inc 100))`

  </td>
  <td>

   A [function](@docroot@/language/syntax.md#functions) bound to a variable and subsequently called by name (evaluates to 103)

  </td>
 </tr>
 <tr>
  <td>

   `{ x, y }: x + y`

  </td>
  <td>

   A [function](@docroot@/language/syntax.md#functions) that expects a set with required attributes `x` and `y` and concatenates them

  </td>
 </tr>
 <tr>
  <td>

   `{ x, y ? "bar" }: x + y`

  </td>
  <td>

   A [function](@docroot@/language/syntax.md#functions) that expects a set with required attribute `x` and optional `y`, using `"bar"` as default value for `y`

  </td>
 </tr>
 <tr>
  <td>

   `{ x, y, ... }: x + y`

  </td>
  <td>

   A [function](@docroot@/language/syntax.md#functions) that expects a set with required attributes `x` and `y` and ignores any other attributes

  </td>
 </tr>
 <tr>
  <td>

   `{ x, y } @ args: x + y`

   `args @ { x, y }: x + y`

  </td>
  <td>

   A [function](@docroot@/language/syntax.md#functions) that expects a set with required attributes `x` and `y`, and binds the whole set to `args`

  </td>
 </tr>
 <tr>
  <td>

   *Built-in functions*

  </td>
  <td>



  </td>
 </tr>
 <tr>
  <td>

   `import ./foo.nix`

  </td>
  <td>

   Load and return Nix expression in given file.
   See [import](@docroot@/language/builtins.md#builtins-import).

  </td>
 </tr>
 <tr>
  <td>

   `map (x: x + x) [ 1 2 3 ]`

  </td>
  <td>

   Apply a function to every element of a list (evaluates to `[ 2 4 6 ]`).
   See [`map`](@docroot@/language/builtins.md#builtins-map).

  </td>
 </tr>
</table>
