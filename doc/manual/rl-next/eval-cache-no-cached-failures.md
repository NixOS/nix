---
synopsis: The flake evaluation cache no longer stores failures
prs: [16679]
issues: 3872
---

An attribute whose evaluation failed is evaluated again on the next use instead of being recorded in the evaluation cache.
A failure that does not repeat, such as an interrupted import from derivation or an evaluator that ran out of memory,
no longer follows the flake until its cache is deleted.
