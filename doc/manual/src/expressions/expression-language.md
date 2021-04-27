# Nix Expression Language

The Nix expression language is a pure, lazy, functional language. Purity
means that operations in the language don't have side-effects (for
instance, there is no variable assignment). Laziness means that
arguments to functions are evaluated only when they are needed.
Functional means that functions are “normal” values that can be passed
around and manipulated in interesting ways. The language is not a
full-featured, general purpose language. Its main job is to describe
packages, compositions of packages, and the variability within packages.

This section presents the various features of the language.
