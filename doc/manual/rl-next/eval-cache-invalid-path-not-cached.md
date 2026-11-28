---
synopsis: Failures from invalid store paths no longer go into the cache for flake evaluation.
prs: [16679]
---

A store path in string context can be invalid, for example after garbage collection. Evaluation of an attribute can fail for that reason. Later uses re-evaluate that attribute instead of failing from the evaluation cache. Other evaluation errors depend only on the flake and stay cached.
