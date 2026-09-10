---
synopsis: "`--no-modify-profile` now works for multi-user installs"
issues: [12134]
prs: [15648]
---

Passing `--no-modify-profile` to the installer now correctly suppresses
shell profile modification for multi-user installations.
Previously, the flag was accepted but silently ignored when installing
in daemon mode: the variable was not exported before handing off to
`install-multi-user`, and that script never checked it regardless.
