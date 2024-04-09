# Contributing documentation

Improvements to documentation are very much appreciated, and a good way to start out with contributing to Nix.

This is how you can help:
- Address [open issues with documentation](https://github.com/NixOS/nix/issues?q=is%3Aissue+is%3Aopen+label%3Adocumentation)
- Review [pull requests concerning documentation](https://github.com/NixOS/nix/pulls?q=is%3Apr+is%3Aopen+label%3Adocumentation)

Incremental refactorings of the documentation build setup to make it faster or easier to understand and maintain are also welcome.

## Building the manual

Build the manual from scratch:

```console
nix-build $(nix-instantiate)'!doc'
```

or

```console
nix build .#^doc
```

and open `./result-doc/share/doc/nix/manual/index.html`.

To build the manual incrementally, [enter the development shell](./hacking.md) and run:

```console
make manual-html-open -j $NIX_BUILD_CORES
```

In order to reflect changes to the [Makefile for the manual], clear all generated files before re-building:

[Makefile for the manual]: https://github.com/NixOS/nix/blob/master/doc/manual/local.mk

```console
rm $(git ls-files doc/manual/ -o | grep -F '.md') && rmdir doc/manual/src/command-ref/new-cli && make manual-html -j $NIX_BUILD_CORES
```

## Style guide

The goal of this style guide is to make it such that
- The manual is easy to search and skim for relevant information
- Documentation sources are easy to edit
- Changes to documentation are easy to review

You will notice that this is not implemented consistently yet.
Please follow the guide when making additions or changes to existing documentation.
Do not make sweeping changes, unless they are programmatic and can be validated easily.

### Language

This manual is [reference documentation](https://diataxis.fr/reference/).
The typical usage pattern is to look up isolated pieces of information.
It should therefore aim to be correct, consistent, complete, and easy to navigate at a glance.

- Aim for clarity and brevity.

  Please take the time to read the [plain language guidelines](https://www.plainlanguage.gov/guidelines/) for details.

- Describe the subject factually.

  In particular, do not make value judgements or recommendations.
  Check the code or add tests if in doubt.

- Provide complete, minimal examples, and explain them.

  Readers should be able to try examples verbatim and get the same results as shown in the manual.
  Always describe in words what a given example does.

  Non-trivial examples may need additional explanation, especially if they use concepts from outside the given context.

- Always explain code examples in the text.

  Use comments in code samples very sparingly, for instance to highlight a particular aspect.
  Readers tend to glance over large amounts of code when scanning for information.

  Especially beginners will likely find reading more complex-looking code strenuous and may therefore avoid it altogether.

  If a code sample appears to require a lot of inline explanation, consider replacing it with a simpler one.
  If that's not possible, break the example down into multiple parts, explain them separately, and then show the combined result at the end.
  This should be a last resort, as that would amount to writing a [tutorial](https://diataxis.fr/tutorials/) on the given subject.

- Use British English.

  This is a somewhat arbitrary choice to force consistency, and accounts for the fact that a majority of Nix users and developers are from Europe.

### Links and anchors

Reference documentation must be readable in arbitrary order.
Readers cannot be expected to have any particular prerequisite knowledge about Nix.
While the table of contents can provide guidance and full-text search can help, they are most likely to find what they need by following sensible cross-references.

- Link to technical terms

  When mentioning Nix-specific concepts, commands, options, settings, etc., link to appropriate documentation.
  Also link to external tools or concepts, especially if their meaning may be ambiguous.
  You may also want to link to definitions of less common technical terms.

  Then readers won't have to actively search for definitions and are more likely to discover relevant information on their own.

  > **Note**
  >
  > `man` and `--help` pages don't display links.
  > Use appropriate link texts such that readers of terminal output can infer search terms.

- Do not break existing URLs between releases.

  There are countless links in the wild pointing to old versions of the manual.
  We want people to find up-to-date documentation when following popular advice.

  - When moving files, update [redirects on nixos.org](https://github.com/NixOS/nixos-homepage/blob/master/netlify.toml).

    This is especially important when moving information out of the Nix manual to other resources.

  - When changing anchors, update [client-side redirects](https://github.com/NixOS/nix/blob/master/doc/manual/redirects.js)

  The current setup is cumbersome, and help making better automation is appreciated.

The build checks for broken internal links with.
This happens late in the process, so [building the whole manual](#building-the-manual) is not suitable for iterating quickly.
[`mdbook-linkcheck`] does not implement checking [URI fragments] yet.

[`mdbook-linkcheck`]: https://github.com/Michael-F-Bryan/mdbook-linkcheck
[URI fragments]: https://en.wikipedia.org/wiki/URI_fragment

### Markdown conventions

The manual is written in markdown, and rendered with [mdBook](https://github.com/rust-lang/mdBook) for the web and with [lowdown](https://github.com/kristapsdz/lowdown) for `man` pages and `--help` output.

For supported markdown features, refer to:
- [mdBook documentation](https://rust-lang.github.io/mdBook/format/markdown.html)
- [lowdown documentation](https://kristaps.bsd.lv/lowdown/)

Please observe these guidelines to ease reviews:

- Write one sentence per line.

  This makes long sentences immediately visible, and makes it easier to review changes and make direct suggestions.

- Use reference links – sparingly – to ease source readability.
  Put definitions close to their first use.

  Example:

  ```
  A [store object] contains a [file system object] and [references] to other store objects.

  [store object]: @docroot@/glossary.md#gloss-store-object
  [file system object]: @docroot@/architecture/file-system-object.md
  [references]: @docroot@/glossary.md#gloss-reference
  ```

- Use admonitions of the following form:

  ```
  > **Note**
  >
  > This is a note.
  ```

  Highlight examples as such:

  ````
  > **Example**
  >
  > ```console
  > $ nix --version
  > ```
  ````

  Highlight syntax definitions as such, using [EBNF](https://en.wikipedia.org/wiki/Extended_Backus%E2%80%93Naur_form) notation:

  ````
  > **Syntax**
  >
  > *attribute-set* = `{` [ *attribute-name* `=` *expression* `;` ... ] `}`
  ````

### The `@docroot@` variable

`@docroot@` provides a base path for links that occur in reusable snippets or other documentation that doesn't have a base path of its own.

If a broken link occurs in a snippet that was inserted into multiple generated files in different directories, use `@docroot@` to reference the `doc/manual/src` directory.

If the `@docroot@` literal appears in an error message from the [`mdbook-linkcheck`] tool, the `@docroot@` replacement needs to be applied to the generated source file that mentions it.
See existing `@docroot@` logic in the [Makefile for the manual].
Regular markdown files used for the manual have a base path of their own and they can use relative paths instead of `@docroot@`.

## API documentation

[Doxygen API documentation] is available online.
You can also build and view it yourself:

[Doxygen API documentation]: https://hydra.nixos.org/job/nix/master/internal-api-docs/latest/download-by-type/doc/internal-api-docs

```console
# nix build .#hydraJobs.internal-api-docs
# xdg-open ./result/share/doc/nix/internal-api/html/index.html
```

or inside `nix-shell` or `nix develop`:

```
# make internal-api-html
# xdg-open ./outputs/doc/share/doc/nix/internal-api/html/index.html
```
