# Nix Language

The Nix language is

- *domain-specific*

  It only exists for the Nix package manager:
  to describe packages and configurations as well as their variants and compositions.
  It is not intended for general purpose use.

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

  Expressions are only evaluated when their value is needed.

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


   *Basic values*


  </td>
  <td>



  </td>
 </tr>
 <tr>
  <td>

   `"hello world"`

  </td>
  <td>

   A string

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

   A multi-line string. Strips common prefixed whitespace. Evaluates to `"multi\n line\n  string"`.

  </td>
 </tr>
 <tr>
  <td>

   `"hello ${ { a = "world" }.a }"`

   `"1 2 ${toString 3}"`

   `"${pkgs.bash}/bin/sh"`

  </td>
  <td>

   String interpolation (expands to `"hello world"`, `"1 2 3"`, `"/nix/store/<hash>-bash-<version>/bin/sh"`)

  </td>
 </tr>
 <tr>
  <td>

   `true`, `false`

  </td>
  <td>

   Booleans

  </td>
 </tr>
 <tr>
  <td>

   `null`

  </td>
  <td>

   Null value

  </td>
 </tr>
 <tr>
  <td>

   `123`

  </td>
  <td>

   An integer

  </td>
 </tr>
 <tr>
  <td>

   `3.141`

  </td>
  <td>

   A floating point number

  </td>
 </tr>
 <tr>
  <td>

   `/etc`

  </td>
  <td>

   An absolute path

  </td>
 </tr>
 <tr>
  <td>

   `./foo.png`

  </td>
  <td>

   A path relative to the file containing this Nix expression

  </td>
 </tr>
 <tr>
  <td>

   `~/.config`

  </td>
  <td>

   A home path. Evaluates to the `"<user's home directory>/.config"`.

  </td>
 </tr>
 <tr>
  <td>

   <nixpkgs>

  </td>
  <td>

   Search path. Value determined by [`$NIX_PATH` environment variable](../command-ref/env-common.md#env-NIX_PATH).

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

   A set with attributes named `x` and `y`

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

   A recursive set, equivalent to `{ x = "foo"; y = "foobar"; }`

  </td>
 </tr>
 <tr>
  <td>

   `[ "foo" "bar" "baz" ]`

   `[ 1 2 3 ]`

   `[ (f 1) { a = 1; b = 2; } [ "c" ] ]`

  </td>
  <td>

   Lists with three elements.

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

   Attribute selection (evaluates to `1`)

  </td>
 </tr>
 <tr>
  <td>

   `{ x = 1; y = 2; }.z or 3`

  </td>
  <td>

   Attribute selection with default (evaluates to `3`)

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

   Conditional expression

  </td>
 </tr>
 <tr>
  <td>

   `assert 1 + 1 == 2; "yes!"`

  </td>
  <td>

   Assertion check (evaluates to `"yes!"`).

  </td>
 </tr>
 <tr>
  <td>

   `let x = "foo"; y = "bar"; in x + y`

  </td>
  <td>

   Variable definition

  </td>
 </tr>
 <tr>
  <td>

   `with builtins; head [ 1 2 3 ]`

  </td>
  <td>

   Add all attributes from the given set to the scope (evaluates to `1`)

  </td>
 </tr>
 <tr>
  <td>

   *Functions (lambdas)*

  </td>
  <td>



  </td>
 </tr>
 <tr>
  <td>

   `x: x + 1`

  </td>
  <td>

   A function that expects an integer and returns it increased by 1

  </td>
 </tr>
 <tr>
  <td>

   `x: y: x + y`

  </td>
  <td>

   Curried function, equivalent to `x: (y: x + y)`. Can be used like a function that takes two arguments and returns their sum.

  </td>
 </tr>
 <tr>
  <td>

   `(x: x + 1) 100`

  </td>
  <td>

   A function call (evaluates to 101)

  </td>
 </tr>
 <tr>
  <td>

   `let inc = x: x + 1; in inc (inc (inc 100))`

  </td>
  <td>

   A function bound to a variable and subsequently called by name (evaluates to 103)

  </td>
 </tr>
 <tr>
  <td>

   `{ x, y }: x + y`

  </td>
  <td>

   A function that expects a set with required attributes `x` and `y` and concatenates them

  </td>
 </tr>
 <tr>
  <td>

   `{ x, y ? "bar" }: x + y`

  </td>
  <td>

   A function that expects a set with required attribute `x` and optional `y`, using `"bar"` as default value for `y`

  </td>
 </tr>
 <tr>
  <td>

   `{ x, y, ... }: x + y`

  </td>
  <td>

   A function that expects a set with required attributes `x` and `y` and ignores any other attributes

  </td>
 </tr>
 <tr>
  <td>

   `{ x, y } @ args: x + y`

   `args @ { x, y }: x + y`

  </td>
  <td>

   A function that expects a set with required attributes `x` and `y`, and binds the whole set to `args`

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

   Load and return Nix expression in given file

  </td>
 </tr>
 <tr>
  <td>

   `map (x: x + x) [ 1 2 3 ]`

  </td>
  <td>

   Apply a function to every element of a list (evaluates to `[ 2 4 6 ]`)

  </td>
 </tr>
</table>
