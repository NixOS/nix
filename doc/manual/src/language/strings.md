# Strings

> <a id="type-string" href="#type-string">*string*</a> = *double-quoted* | *multiline* | *indented*
>
> *double-quoted* = `"` (*char* | *doubleq-escaped* | *interpolated-expr*)* `"`
>
> *doubleq-escaped* = `\` (`n` | `r` | `t` | `"` | `${`)
>
> *interpolated-expr* = `${` *string-valued-expression* `}`
>
> *multiline* = *double-quoted*
>
> *indented* = `''` (*char* | *indented-escaped* | *interpolated-expr*)* `''`
>
> *indented-escaped* = `'` `''` | `''` `${` | `''\` (`n` | `r` | `t` | `\`) | `$` `$`


## Quoted string

> *double-quoted* = `"` (*char* | *doubleq-escaped* | *interpolated-expr*)* `"`
>
> *doubleq-escaped* = `\` (`n` | `r` | `t` | `"` | `${` | `\`)

TODO: what happens to the character inserted by pressing tab on a keyboard, in an editor that does not convert tabs to spaces?

### Escaping characters in quoted strings

The following characters require escaping in quoted strings:
* `\n`: newline
* `\r`: carriage return
* `\t`: tab
* `"`: double quote
* `${`: start of a [string interpolation] block

  [string interpolation]: ./string-interpolation.md

## Multiline string

> *multiline* = *double-quoted*

Unescaped newline characters will be ignored in a quoted string, as they are characters requiring escapes.
Therefore, double-quoted strings can span multiple lines.

## Indented string

> *indented* = `''` (*char* | *indented-escaped* | *interpolated-expr*)* `''`
>
> *indented-escaped* = `'` `''` | `''` `${` | `''\` (`n` | `r` | `t` | `\`) | `$` `$`

In an indented string, the following characters are automatically stripped (an "empty" line is a line that only contains whitespalce):
* from the first line: the first newline character, if the first line is an empty line
* from every line: the minimum number of whitespaces contained by all *non-empty* lines

Indented strings are primarily useful in that they allow multi-line string literals to follow the indentation of the enclosing Nix expression, and that less escaping is typically necessary for strings representing languages such as shell scripts and configuration files because `''` is much less common than `"`.

### Example: general usefulness
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

### Example: automatic stripping of indentation
Input (`.` represents a whitespace character):
```nix
  ''
    This is the first line
    This is the second line
      This is the third line
  '' # "empty-line", it has no non-whitespace characters
```

Is equivalent to:
```nix
"This is the first line.\nThis is the second line.\n  This is the third line.\n"
```

### Example: escaping special characters

TODO

## Glossary

<a name="gloss-string">**Unicode character:**</a> Wikipedia link to Unicode article?

<a name="gloss-string-byte-string">**Byte string:**</a> a sequence of characters in byte form. This is the "raw" form of Nix strings.

<a name="gloss-string-processor">**String processor:**</a> a Nix string is processed from its source code representation into its byte string representation by an appropriate string processor depending on the type of string (double-quoted or indented).

<a name="gloss-strings-sig-char">**Significant character:**</a> a character in the body of a string that will appear as is in the final byte representation of a string.
 lines (a "non-empty" line contains some non-whitespace characters)

<a name="gloss-strings-special-char">**Special substring:**</a> a character in the body of a string that usually has special meaning to a particular string processor. It must be escaped to be treated as a normal character.

TODO: probably many others!