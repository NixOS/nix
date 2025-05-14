---
synopsis: "Amend OSC 8 escape stripping for xterm-style separator"
issues:
prs: [13109]
---

Improve terminal escape code filtering to understand a second type of hyperlink escape codes.
This in particular prevents parts of GCC 14's diagnostics from being improperly filtered away.
