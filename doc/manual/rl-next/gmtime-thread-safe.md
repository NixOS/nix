---
synopsis: "Formatting `lastModified` dates is thread-safe"
prs: [16743]
---

Nix formatted `lastModified` dates with `gmtime`, which returns a buffer that all threads share.
In a program that runs several evaluators in one process, two threads that evaluated flakes or `fetchTree` calls at the same time could get the wrong `lastModifiedDate` or print the wrong date.
The `nix` command evaluates on one thread and is not affected.
Nix now uses `gmtime_r` and `localtime_r` (`gmtime_s` and `localtime_s` on Windows).
