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

## Basic values

### Integer {#type-int}

An _integer_ in Nix is a signed 64-bit integer.

Non-negative integers are expressible as [integer literals](constructs.md#number-literal).
The function [`builtins.isInt`](builtins.md#builtins-isInt) can be used to determine if a value is an integer.

### Float {#type-float}

A _float_ in Nix is a 64-bit IEEE 754 floating-point number.

Most non-negative floats are expressible as [float literals](constructs.md#number-literal).
The function [`builtins.isFloat`](builtins.md#builtins-isFloat) can be used to determine if a value is a float.

### Boolean {#type-bool}

A _boolean_ in Nix is one of _true_ or _false_.

These values are available in the [initial environment](initial-environment.md) as `true` and `false`,
and as attributes on the [`builtins`](builtin-constants.md#builtins-builtins) attribute set as [`builtins.true`](builtin-constants.md#builtins-true) and [`builtins.false`](builtin-constants.md#builtins-false).
The function [`builtins.isBool`](builtins.md#builtins-isBool) can be used to determine if a value is a boolean.

### String {#type-string}

A _string_ in Nix is an immutable, finite-length sequence of bytes, along with a [string context](string-context.md).
Nix does not assume or support working natively with character encodings.

Context-free string values are expressible as [string literals](constructs.md#string-literal).
The function [`builtins.isString`](builtins.md#builtins-isString) can be used to determine if a value is a string.

### Path {#type-path}

<!-- TODO(@rhendric, #10970): Incorporate content from constructs.md#path-literal -->

The function [`builtins.isPath`](builtins.md#builtins-isPath) can be used to determine if a value is a path.

### Null {#type-null}

There is a single value of type _null_ in Nix.

This value is available in the [initial environment](initial-environment.md) as `null`,
and as an attribute on the [`builtins`](builtin-constants.md#builtins-builtins) attribute set as [`builtins.null`](builtin-constants.md#builtins-null).

## Compound values

### Attribute set {#type-attrs}

<!-- TODO(@rhendric, #10970): fill this out -->

An attribute set can be constructed with an [attribute set literal](constructs.md#attrs-literal).
The function [`builtins.isAttrs`](builtins.md#builtins-isAttrs) can be used to determine if a value is an attribute set.

### List {#type-list}

<!-- TODO(@rhendric, #10970): fill this out -->

A list can be constructed with a [list literal](constructs.md#list-literal).
The function [`builtins.isList`](builtins.md#builtins-isList) can be used to determine if a value is a list.

## Function {#type-function}

<!-- TODO(@rhendric, #10970): fill this out -->

A function can be constructed with a function expression.
The function [`builtins.isFunction`](builtins.md#builtins-isFunction) can be used to determine if a value is a function.

## External {#type-external}

An _external_ value is an opaque value created by a Nix [plugin](../command-ref/conf-file.md#conf-plugin-files).
It can be substituted in Nix expressions but it can only be created and used by plugin code.
