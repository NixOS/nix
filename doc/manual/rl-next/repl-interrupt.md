---
synopsis: Interrupting REPL commands works more than once
issues: [13481]
---

Previously, this only worked once per REPL session; further attempts would be ignored.
This issue is now fixed, so REPL commands such as `:b` or `:p` can be canceled consistently.
This is a cherry-pick of the change from the [Lix project](https://gerrit.lix.systems/c/lix/+/1097).
