# Data Types

Every value in the Nix language has one of the following types:

* [Integer](#type-int)
* [Float](#type-float)
* [Boolean](#type-bool)
* [String](#type-string)
* [Path](#type-path)
* [Null](#type-null)
* [Attribute set](#type-attrs)
* [List](#type-list)
* [Function](#type-function)
* [External](#type-external)

## Primitives

### Integer {#type-int}

An _integer_ in the Nix language is a signed 64-bit integer.

Non-negative integers can be expressed as [integer literals](syntax.md#number-literal).
Negative integers are created with the [arithmetic negation operator](./operators.md#arithmetic).
The function [`builtins.isInt`](builtins.md#builtins-isInt) can be used to determine if a value is an integer.

### Float {#type-float}

A _float_ in the Nix language is a 64-bit [IEEE 754](https://en.wikipedia.org/wiki/IEEE_754) floating-point number.

Most non-negative floats can be expressed as [float literals](syntax.md#number-literal).
Negative floats are created with the [arithmetic negation operator](./operators.md#arithmetic).
The function [`builtins.isFloat`](builtins.md#builtins-isFloat) can be used to determine if a value is a float.

### Boolean {#type-bool}

A _boolean_ in the Nix language is one of _true_ or _false_.

<!-- TODO: mention the top-level environment -->

These values are available as attributes of [`builtins`](builtins.md#builtins-builtins) as [`builtins.true`](builtins.md#builtins-true) and [`builtins.false`](builtins.md#builtins-false).
The function [`builtins.isBool`](builtins.md#builtins-isBool) can be used to determine if a value is a boolean.

### String {#type-string}

A _string_ in the Nix language is an immutable, finite-length sequence of bytes, along with a [string context](string-context.md).
Nix does not assume or support working natively with character encodings.

String values without string context can be expressed as [string literals](string-literals.md).
The function [`builtins.isString`](builtins.md#builtins-isString) can be used to determine if a value is a string.

### Path {#type-path}

A _path_ in the Nix language is an immutable, finite-length sequence of bytes starting with `/`, representing a POSIX-style, canonical file system path.
Path values are distinct from string values, even if they contain the same sequence of bytes.
Operations that produce paths will simplify the result as the standard C function [`realpath`] would, except that there is no symbolic link resolution.

[`realpath`]: https://pubs.opengroup.org/onlinepubs/9699919799/functions/realpath.html

Paths are suitable for referring to local files, and are often preferable over strings.
- Path values do not contain trailing or duplicate slashes, `.`, or `..`.
- Relative path literals are automatically resolved relative to their [base directory].
- Tooling can recognize path literals and provide additional features, such as autocompletion, refactoring automation and jump-to-file.

[base directory]: @docroot@/glossary.md#gloss-base-directory

A file is not required to exist at a given path in order for that path value to be valid, but a path that is converted to a string with [string interpolation] or [string-and-path concatenation] must resolve to a readable file or directory which will be copied into the Nix store.
For instance, evaluating `"${./foo.txt}"` will cause `foo.txt` from the same directory to be copied into the Nix store and result in the string `"/nix/store/<hash>-foo.txt"`.
Operations such as [`import`] can also expect a path to resolve to a readable file or directory.

[string interpolation]: string-interpolation.md#interpolated-expression
[string-and-path concatenation]: operators.md#string-and-path-concatenation
[`import`]: builtins.md#builtins-import

> **Note**
>
> The Nix language assumes that all input files will remain _unchanged_ while evaluating a Nix expression.
> For example, assume you used a file path in an interpolated string during a `nix repl` session.
> Later in the same session, after having changed the file contents, evaluating the interpolated string with the file path again might not return a new [store path], since Nix might not re-read the file contents.
> Use `:r` to reset the repl as needed.

[store path]: @docroot@/store/store-path.md

Path values can be expressed as [path literals](syntax.md#path-literal).
The function [`builtins.isPath`](builtins.md#builtins-isPath) can be used to determine if a value is a path.

### Null {#type-null}

There is a single value of type _null_ in the Nix language.

<!-- TODO: mention the top-level environment -->

This value is available as an attribute on the [`builtins`](builtins.md#builtins-builtins) attribute set as [`builtins.null`](builtins.md#builtins-null).

## Compound values

### Attribute set {#type-attrs}

<!-- TODO(@rhendric, #10970): fill this out -->

An attribute set can be constructed with an [attribute set literal](syntax.md#attrs-literal).
The function [`builtins.isAttrs`](builtins.md#builtins-isAttrs) can be used to determine if a value is an attribute set.

### List {#type-list}

<!-- TODO(@rhendric, #10970): fill this out -->

A list can be constructed with a [list literal](syntax.md#list-literal).
The function [`builtins.isList`](builtins.md#builtins-isList) can be used to determine if a value is a list.

## Function {#type-function}

<!-- TODO(@rhendric, #10970): fill this out -->

A function can be constructed with a [function expression](syntax.md#functions).
The function [`builtins.isFunction`](builtins.md#builtins-isFunction) can be used to determine if a value is a function.

## External {#type-external}

An _external_ value is an opaque value created by a Nix [plugin](../command-ref/conf-file.md#conf-plugin-files).
Such a value can be substituted in Nix expressions but only created and used by plugin code.
