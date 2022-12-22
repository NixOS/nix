# Operators

## Attribute selection

> *e* `.` *attrpath* \[ `or` *def* \]

Select the attribute denoted by attribute path *attrpath* from attribute set *e*.
An attribute path is a dot-separated list of attribute names.
If the attribute doesn’t exist, return *def* if provided, otherwise abort evaluation.

Associativity: none

Precedence: 1

## Function application

> *f* *e*

Call function *f* with argument *e*.

Associativity: left

Precedence: 2

## Arithmetic negation

> `-` *e*

Associativity: none

Precedence: 3

## Has attribute

> *e* `?` *attrpath*

Test whether set *e* contains the attribute denoted by *attrpath*; return `true` or `false`.

Associativity: none

Precedence: 4

## List concatenation

> *e1* `++` *e2*

Concatenate lists *e1* and *e2*.

Associativity: right

Precedence: 5

## Multiplication

> *e1* `*` *e2*,

Multiply numbers *e1* and *e2*.

Associativity: left

Precedence: 6

## Division

> *e1* `/` *e2*

Divide numbers *e1* and *e2*.

Associativity: left

Precedence: 6

## Addition

> *e1* `+` *e2*

Add numbers *e1* and *e2*.

Associativity: left

Precedence: 7

## Subtraction

> *e1* `-` *e2*

Subtract numbers *e2* from *e1*.

Associativity: left

Precedence: 7

## String concatenation

> *string1* `+` *string2*

Concatenate *string1* and *string1* and merge their string contexts.

Associativity: left

Precedence: 7

## Logical negation (`NOT`)

> `!` *e*

Negate the Boolean value *e*.

Associativity: none

Precedence: 8

## Merge attribute sets

> *e1* `//` *e2*

Return a set consisting of all the attributes in *e1* and *e2*.
If an attribute name is present in both, the attribute value from the former is taken.

Associativity: right

Precedence: 9

## Less than

> *e1* `<` *e2*,

- Arithmetic comparison for numbers
- Lexicographic comparison for strings and paths
- Lexicographic comparison for lists:
  Elements at the same index in both lists are compared according to their type and skipped if they are equal.

Associativity: none

Precedence: 10

## Less than or equal to

> *e1* `<=` *e2*

- Arithmetic comparison for numbers
- Lexicographic comparison for strings and paths
- Lexicographic comparison for lists:
  Elements at the same index in both lists are compared according to their type and skipped if they are equal.

Associativity: none

Precedence: 10

## Greater than

> *e1* `>` *e2*

- Arithmetic comparison for numbers
- Lexicographic comparison for strings and paths
- Lexicographic comparison for lists:
  Elements at the same index in both lists are compared according to their type and skipped if they are equal.

Associativity: none

Precedence: 10

## Greater than or equal to

> *e1* `>=` *e2*

- Arithmetic comparison for numbers
- Lexicographic comparison for strings and paths
- Lexicographic comparison for lists:
  Elements at the same index in both lists are compared according to their type and skipped if they are equal.

Associativity: none

Precedence: 10

## Equality

> *e1* `==` *e2*

Check *e1* and *e2* for equality.

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

> *e1* `&&` *e2*

Return `true` if and only if both `e1` and `e2` evaluate to `true`, otherwise `false`.

Associativity: left

Precedence: 12

## Logical disjunction (`OR`)

> *e1* `||` *e2*

Return `true` if at least `e1` or `e2` evaluate to `true`, otherwise `false`.

Associativity: left

Precedence: 13

## Logical implication

>  *e1* `->` *e2*

Return `false` if *e1* evaluates to `true` and *e2* evaluates to `false`, otherwise `true`.

Equivalent to `!`*e1* `||` *e2*.

Associativity: none

Precedence: 14
