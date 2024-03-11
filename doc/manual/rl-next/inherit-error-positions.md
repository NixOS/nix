---
synopsis: fix duplicate attribute error positions for `inherit`
prs: 9874
---

When an inherit caused a duplicate attribute error the position of the error was not reported correctly, placing the error with the inherit itself or at the start of the bindings block instead of the offending attribute name.
