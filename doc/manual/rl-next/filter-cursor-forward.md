---
synopsis: "Build output keeps spaces written as cursor movements"
issues: [14797]
prs: [16713]
---

When build output is filtered for a destination that is not a terminal, a cursor forward escape (`ESC [ n C`) is now written as *n* spaces instead of being dropped.
Some programs, such as Wine, write spaces this way.
