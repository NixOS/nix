# libcmarkcpp

A C++ terminal renderer for CommonMark documents.

## Overview

libcmarkcpp provides a terminal renderer for CommonMark (Markdown) documents using the cmark library. It renders formatted, colored output suitable for display in ANSI-capable terminals.

## Features

- ANSI color styling and text formatting (bold, italic, underline)
- Intelligent text wrapping and indentation
- Support for:
  - Headers with hierarchical styling
  - Lists (ordered and unordered)
  - Code blocks (fenced and indented)
  - Blockquotes
  - Links with OSC8 hyperlink support
  - Inline code, bold, and italic
  - Horizontal rules
  - Images
- Terminal width detection and adaptive wrapping
- Configurable margins, padding, and styling options

## Origin

This library is a C++ port of the terminal renderer from [lowdown](https://github.com/kristapsdz/lowdown) by Kristaps Dzonsons, adapted to work with the [cmark](https://github.com/commonmark/cmark) CommonMark implementation.

## License

ISC License (same as lowdown)

## Dependencies

- cmark >= 0.31.0
- C++20 compiler
