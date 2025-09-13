---
synopsis: Derivation JSON format now uses store path basenames (no store dir) only
prs: [13980]
issues: [13570]
---

Experience with many JSON frameworks (e.g. nlohmann/json in C++, Serde
in Rust, and Aeson in Haskell), has show that the use of the store dir
in JSON formats is an impediment to systematic JSON formats, because it
requires the serializer/deserializer to take an extra paramater (the
store dir).

We ultimately want to rectify this issue with all (non-stable, able to
be changed) JSON formats. To start with, we are changing the JSON format
for derivations because the `nix derivation` commands are --- in
addition to being formally unstable --- less widely used than other
unstable commands.
