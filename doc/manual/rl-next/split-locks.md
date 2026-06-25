---
synopsis: Split locks
issues: 10740 2589 2029
prs: 15856
---

New remote only locks are used when [`avoid-local`]{#conf-avoid-local} is true.
To further aid recursive setups, a new builder parameter is added which when
true, disable build hooks (remote builds) on the builder.

Build hooks will now need to hold the local lock when copying outputs from a
remote builder if [`avoid-local`]{#conf-avoid-local} is true.
