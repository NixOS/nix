# Operators

## Select

> *e* `.` *attrpath* \[ `or` *def* \]

Select attribute denoted by the attribute path *attrpath* from set *e*. (An attribute path is a dot-separated list of attribute names.) If the attribute doesn’t exist, return *def* if provided, otherwise abort evaluation.

Associativity: none

Precedence: 1

## Application

> *e1* *e2*

Call function *e1* with argument *e2*.

Associativity: left

Precedence: 2

## Arithmetic Negation

> `-` *e*

Arithmetic negation.

Associativity: none

Precedence: 3

## Has Attribute

> *e* `?` *attrpath*

Test whether set *e* contains the attribute denoted by *attrpath*; return `true` or `false`.

Associativity: none

Precedence: 4

## List Concatenation

> *e1* `++` *e2*

List concatenation.

Associativity: right

Precedence: 5

## Multiplication

> *e1* `*` *e2*,

Arithmetic multiplication.

Associativity: left

Precedence: 6

## Division

> *e1* `/` *e2*

Arithmetic division.

Associativity: left

Precedence: 6

## Addition

> *e1* `+` *e2*

Arithmetic addition.

Associativity: left

Precedence: 7

## Subtraction

> *e1* `-` *e2*

Arithmetic subtraction.

Associativity: left

Precedence: 7

## String Concatenation

> *string1* `+` *string2*

String concatenation.

Associativity: left

Precedence: 7

## Not

> `!` *e*

Boolean negation.

Associativity: none

Precedence: 8

## Update

> *e1* `//` *e2*

Return a set consisting of the attributes in *e1* and *e2* (with the latter taking precedence over the former in case of equally named attributes).

Associativity: right

Precedence: 9

## Less Than

> *e1* `<` *e2*,

Arithmetic/lexicographic comparison.

Associativity: none

Precedence: 10

## Less Than or Equal To

> *e1* `<=` *e2*

Arithmetic/lexicographic comparison.

Associativity: none

Precedence: 10

## Greater Than

> *e1* `>` *e2*

Arithmetic/lexicographic comparison.

Associativity: none

Precedence: 10

## Greater Than or Equal To

> *e1* `>=` *e2*

Arithmetic/lexicographic comparison.

Associativity: none

Precedence: 10

## Equality

> *e1* `==` *e2*

Equality.

Associativity: none

Precedence: 11

## Inequality

> *e1* `!=` *e2*

Inequality.

Associativity: none

Precedence: 11

## Logical AND

> *e1* `&&` *e2*

Logical AND.

Associativity: left

Precedence: 12

## Logical OR

> *e1* <code>&#124;&#124;</code> *e2*

Logical OR.

Associativity: left

Precedence: 13

## Logical Implication

> *e1* `->` *e2*

Logical implication (equivalent to <code>!e1 &#124;&#124; e2</code>).

Associativity: none

Precedence: 14

