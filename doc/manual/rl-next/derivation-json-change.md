---
synopsis: Modify `nix derivation {add,show}` JSON format
issues: 9866
prs: 10722
---

The JSON format for derivations has been slightly revised to better conform to our [JSON guidelines](@docroot@contributing/cli-guideline#returning-future-proof-json).
In particular, the hash algorithm and content addressing method of content-addresed derivation outputs is now separated into two fields `hashAlgo` and `method`,
rather than one field with an arcane `:`-separated format.

This JSON format is only used by the experimental `nix derivation` family of commands, at this time.
Future revisions are expected as the JSON format is still not entirely in compliance even after these changes.
