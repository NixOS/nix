---
synopsis: consistent order of lambda formals in printed expressions
prs: 9874
---

Always print lambda formals in lexicographic order rather than the internal, creation-time based symbol order.
This makes printed formals independent of the context they appear in.
