---
synopsis: "Detect cycles when converting values to JSON and XML"
issues: [12289]
---

Converting a self-referential value (e.g. `let a = { b = a; }; in a`) with
`builtins.toJSON`/`nix eval --json` or `builtins.toXML`/`nix-instantiate --xml`
previously failed with an opaque stack-overflow error, or streamed deeply
nested output until the call-depth limit was eventually hit. Cycles are now
detected explicitly and an `infinite recursion` error is raised.
