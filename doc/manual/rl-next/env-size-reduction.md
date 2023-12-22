---
synopsis: Reduce eval memory usage and wall time
prs: 9658
---

Reduce the size of the `Env` struct used in the evaluator by a pointer, or 8 bytes on most modern machines.
This reduces memory usage during eval by around 2% and wall time by around 3%.
