---
synopsis: Revert incomplete closure mixed download and build feature
issues: [77, 12628]
prs: [13176]
---

Since Nix 1.3 (299141ecbd08bae17013226dbeae71e842b4fdd7 in 2013) Nix has attempted to mix together upstream fresh builds and downstream substitutions when remote substuters contain an "incomplete closure" (have some store objects, but not the store objects they reference).
This feature is now removed.

Worst case, removing this feature could cause more building downstream, but it should not cause outright failures, since this is not happening for opaque store objects that we don't know how to build if we decide not to substitute.
In practice, however, we doubt even the more building is very likely to happen.
Remote stores that are missing dependencies in arbitrary ways (e.g. corruption) don't seem to be very common.

On the contrary, when remote stores fail to implement the [closure property](@docroot@/store/store-object.md#closure-property), it is usually an *intentional* choice on the part of the remote store, because it wishes to serve as an "overlay" store over another store, such as `https://cache.nixos.org`.
If an "incomplete closure" is encountered in that situation, the right fix is not to do some sort of "franken-building" as this feature implemented, but instead to make sure both substituters are enabled in the settings.

(In the future, we should make it easier for remote stores to indicate this to clients, to catch settings that won't work in general before a missing dependency is actually encountered.)
