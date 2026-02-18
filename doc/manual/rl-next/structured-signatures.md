---
synopsis: Signatures are now a structured type
significance: significant
---

[Signatures](@docroot@/protocols/json/signature.md) in JSON formats are now represented as structured objects with `keyName` and `sig` fields, rather than colon-separated strings.
`nix path-info --json --json-format 3` opts into the new version for this command.

JSON parsing accepts both the old string format and new structured format for backwards compatibility.
