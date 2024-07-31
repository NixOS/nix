---
synopsis: "Solve `cached failure of attribute X`"
prs: 10564
issues: 10513 9165
---

This eliminates all "cached failure of attribute X" messages by forcing evaluation of the original value when needed to show the exception to the user. This enhancement improves error reporting by providing the underlying message and stack trace.

Author: [**Eelco Dolstra (@edolstra)**](https://github.com/edolstra)
