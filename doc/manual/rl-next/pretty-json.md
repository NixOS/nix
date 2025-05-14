---
synopsis: "Prettified JSON output on the terminal"
issues: 12555
prs: [12652]
---

This makes the output easier to read.

Scripts are mostly unaffected because for those, stdout will be a file or a pipe, not a terminal, and for those, the old single-line behavior applies.

`--json --pretty` can be passed to enable it even if the output is not a terminal.
If your script creates a pseudoterminal for Nix's stdout, you can pass `--no-pretty` to disable the new behavior.
