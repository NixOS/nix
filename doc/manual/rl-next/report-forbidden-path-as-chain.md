---
synopsis: "`disallowedRequisites` now reports chains of disallowed requisites"
# issues: ...
prs: 10877
---

When a build fails because of [`disallowedRequisites`](@docroot@/language/advanced-attributes.md#adv-attr-disallowedRequisites), the error message now includes the chain of references that led to the failure. This makes it easier to see in which derivations the chain can be broken, to resolve the problem.

Example:

```
$ nix build --no-link /nix/store/1fsjsd98awcf3sgny9zfq4wa7i5dci4n-hercules-ci-agent-0.10.3.drv^out
error: output '/nix/store/xg01gga2qsi9v2qm45r2gqrkk46wvrkd-hercules-ci-agent-0.10.3' is not allowed to refer to the following paths.
       Shown below are chains that lead to the forbidden path(s). More may exist.
         { /nix/store/xg01gga2qsi9v2qm45r2gqrkk46wvrkd-hercules-ci-agent-0.10.3 -> /nix/store/mfzkjrwv8ckqqbsbj5yj24ri9dsgs30l-hercules-ci-cnix-expr-0.3.6.3 -> /nix/store/vxlw2igrncif77fh2qg1jg02bd455mbl-ghc-9.6.5 }
```
