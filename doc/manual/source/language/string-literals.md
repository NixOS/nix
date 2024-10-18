# String literals

A *string literal* represents a [string](types.md#type-string) value.

> **Syntax**
>
> *expression* → *string*
>
> *string* → `"` ( *string_char*\* [*interpolation_element*][string interpolation] )* *string_char*\* `"`
>
> *string* → `''` ( *indented_string_char*\* [*interpolation_element*][string interpolation] )* *indented_string_char*\* `''`
>
> *string* → *uri*
>
> *string_char* ~ `[^"$\\]|\$(?!\{)|\\.`
>
> *indented_string_char* ~ `[^$']|\$\$|\$(?!\{)|''[$']|''\\.|'(?!')`
>
> *uri* ~ `[A-Za-z][+\-.0-9A-Za-z]*:[!$%&'*+,\-./0-9:=?@A-Z_a-z~]+`

Strings can be written in three ways.

The most common way is to enclose the string between double quotes, e.g., `"foo bar"`.
Strings can span multiple lines.
The results of other expressions can be included into a string by enclosing them in `${ }`, a feature known as [string interpolation].

[string interpolation]: ./string-interpolation.md

The following must be escaped to represent them within a string, by prefixing with a backslash (`\`):

- Double quote (`"`)

> **Example**
>
> ```nix
> "\""
> ```
>
>     "\""

- Backslash (`\`)

> **Example**
>
> ```nix
> "\\"
> ```
>
>     "\\"

- Dollar sign followed by an opening curly bracket (`${`) – "dollar-curly"

> **Example**
>
> ```nix
> "\${"
> ```
>
>     "\${"

The newline, carriage return, and tab characters can be written as `\n`, `\r` and `\t`, respectively.

A "double-dollar-curly" (`$${`) can be written literally.

> **Example**
>
> ```nix
> "$${"
> ```
>
>     "$\${"

String values are output on the terminal with Nix-specific escaping.
Strings written to files will contain the characters encoded by the escaping.

The second way to write string literals is as an *indented string*, which is enclosed between pairs of *double single-quotes* (`''`), like so:

```nix
''
This is the first line.
This is the second line.
  This is the third line.
''
```

This kind of string literal intelligently strips indentation from
the start of each line. To be precise, it strips from each line a
number of spaces equal to the minimal indentation of the string as a
whole (disregarding the indentation of empty lines). For instance,
the first and second line are indented two spaces, while the third
line is indented four spaces. Thus, two spaces are stripped from
each line, so the resulting string is

```nix
"This is the first line.\nThis is the second line.\n  This is the third line.\n"
```

> **Note**
>
> Whitespace and newline following the opening `''` is ignored if there is no non-whitespace text on the initial line.

> **Warning**
>
> Prefixed tab characters are not stripped.
>
> > **Example**
> >
> > The following indented string is prefixed with tabs:
> >
> > <pre><code class="nohighlight">''
> > 	all:
> > 		@echo hello
> > ''
> > </code></pre>
> >
> >     "\tall:\n\t\t@echo hello\n"

Indented strings support [string interpolation].

The following must be escaped to represent them in an indented string:

- `$` is escaped by prefixing it with two single quotes (`''`)

> **Example**
>
> ```nix
> ''
>   ''$
> ''
> ```
>
>     "$\n"

- `''` is escaped by prefixing it with one single quote (`'`)

> **Example**
>
> ```nix
> ''
>   '''
> ''
> ```
>
>     "''\n"

These special characters are escaped as follows:
- Linefeed (`\n`): `''\n`
- Carriage return (`\r`): `''\r`
- Tab (`\t`): `''\t`

`''\` escapes any other character.

A "dollar-curly" (`${`) can be written as follows: 
> **Example**
>
> ```nix
> ''
>   echo ''${PATH}
> ''
> ```
>
>     "echo ${PATH}\n"

> **Note**
>
> This differs from the syntax for escaping a dollar-curly within double quotes (`"\${"`). Be aware of which one is needed at a given moment.

A "double-dollar-curly" (`$${`) can be written literally.

> **Example**
>
> ```nix
> ''
>   $${
> ''
> ```
>
>     "$\${\n"

Indented strings are primarily useful in that they allow multi-line
string literals to follow the indentation of the enclosing Nix
expression, and that less escaping is typically necessary for
strings representing languages such as shell scripts and
configuration files because `''` is much less common than `"`.
Example:

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

Finally, as a convenience, *URIs* as defined in appendix B of
[RFC 2396](http://www.ietf.org/rfc/rfc2396.txt) can be written *as
is*, without quotes. For instance, the string
`"http://example.org/foo.tar.bz2"` can also be written as
`http://example.org/foo.tar.bz2`.
