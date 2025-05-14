---
synopsis: "Fix chopped up repl output"
issues: [12599]
prs: [12604]
---

The REPL and logger now contend for standard output in a much more systematic manner.
In particular, the synchronisation mechanism is more error-tolerant, by virtue of now using [RAII](https://en.wikipedia.org/wiki/Resource_acquisition_is_initialization).
REPL output should no longer interrupt or be interrupted by the logger mid-message,
