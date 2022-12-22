# Operators

## Attribute selection

> *attrset* `.` *attrpath* \[ `or` *value* \]

Select the attribute denoted by attribute path *attrpath* from attribute set *attrset*.
An attribute path is a dot-separated list of attribute names.
If the attribute doesn’t exist, return *value* if provided, otherwise abort evaluation.

Associativity: none

Precedence: 1

## Function application

> *f* *a*

Call function *f* with argument *a*.

Associativity: left

Precedence: 2

## Arithmetic negation

> `-` *n*

Flip the sign of the number *n*.

Associativity: none

Precedence: 3

## Has attribute

> *attrset* `?` *attrpath*

Test whether attribute set *attrset* contains the attribute denoted by *attrpath*; return `true` or `false`.

Associativity: none

Precedence: 4

## List concatenation

> *list1* `++` *list2*

Concatenate lists *list1* and *list2*.

Associativity: right

Precedence: 5

## Multiplication

> *n1* `*` *n2*,

Multiply numbers *n1* and *n2*.

Associativity: left

Precedence: 6

## Division

> *n1* `/` *n2*

Divide numbers *n1* and *n2*.

Associativity: left

Precedence: 6

## Subtraction

> *n1* `-` *n2*

Subtract numbers *n2* from *n1*.

Associativity: left

Precedence: 7

## Addition

> *n1* `+` *n2*

Add numbers *n1* and *n2*.

Associativity: left

Precedence: 7

## String concatenation

> *string1* `+` *string2*

Concatenate *string1* and *string1* and merge their string contexts.

Associativity: left

Precedence: 7

## Path concatenation

> *path1* `+` *path2*

Concatenate two paths.
The result is a path.

## Path and string concatenation

> *path* `+` *string*

Concatenate *path* with *string*.
The result is a path.

> **Note**
>
> The string must not have a string context that refers to a store path.

Associativity: left

Precedence: 7

## String and path concatenation

> *string* `+` *path*

Concatenate *string* with *path*.
The result is a string.

> **Important**
>
> The file or directory at *path* must exist and is copied to the store
> The path appears in the result as the corresponding store path.

Associativity: left

Precedence: 7

## Logical negation (`NOT`)

> `!` *b*

Negate the Boolean value *b*.

Associativity: none

Precedence: 8

## Update

> *attrset1* `//` *attrset1*

Update attribute set *attrset1* with names and values from *attrset2*.

The returned attribute set will have of all the attributes in *e1* and *e2*.
If an attribute name is present in both, the attribute value from the former is taken.

Associativity: right

Precedence: 9

## Comparison

- Arithmetic comparison for numbers
- Lexicographic comparison for strings and paths
- Lexicographic comparison for lists:
  Elements at the same index in both lists are compared according to their type and skipped if they are equal.

Associativity: none

Precedence: 10

### Less than

> *e1* `<` *e2*,

### Less than or equal to

> *e1* `<=` *e2*

### Greater than

> *e1* `>` *e2*

### Greater than or equal to

> *e1* `>=` *e2*

## Equality

> *e1* `==` *e2*

Check expressions *e1* and *e2* for value equality.

- Attribute sets and lists are compared recursively, and therefore are fully evaluated.
- Comparison of functions always returns `false`.
- Integers are coerced to floating point numbers if compared to floating point numbers.
- Floating point numbers only differ up to a limited precision.

Associativity: none

Precedence: 11

## Inequality

> *e1* `!=` *e2*

Equivalent to `! (`*e1* `==` *e2* `)`

Associativity: none

Precedence: 11

## Logical conjunction (`AND`)

> *b1* `&&` *b2*

Return `true` if and only if both `b1` and `b2` evaluate to `true`, otherwise `false`.

Associativity: left

Precedence: 12

## Logical disjunction (`OR`)

> *b1* `||` *b2*

Return `true` if at least one of  `b1` or `b2` evaluate to `true`, otherwise `false`.

Associativity: left

Precedence: 13

## Logical implication

>  *b1* `->` *b2*

Return `false` if *b1* evaluates to `true` and *b2* evaluates to `false`, otherwise `true`.

Equivalent to `!`*b1* `||` *b2*.

Associativity: none

Precedence: 14
