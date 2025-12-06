# Nix Store

The *Nix store* is an abstraction to store immutable file system data (such as software packages) that can have dependencies on other such data.

There are [multiple types of Nix stores](./types/index.md) with different capabilities, such as the default one on the [local filesystem](./types/local-store.md) (`/nix/store`) or [binary caches](./types/http-binary-cache-store.md).

## Two models, abstract and concrete

The Nix store layer is the heart of Nix, the cornerstone of its design.
It comes from two basic insights: a vision for build systems in the abstract based on functional programming, and an application of the vision to conventional software for conventional operating system.
We could just present the combination of those two in the form of the current design of the Nix store, but we believe there is value introducing them separately.
This still describes how Nix works, so this section still serves as a spec, but it also demonstrates with Nix's authors believe is a good way to think* about Nix.
If one tries to learn the concrete specifics before learning the abstract model, the following text might come across as a wall of details without sufficient motivation.
Conversely, if one learns the abstract model first, many of the concrete specifics will make more sense as miscellaneous details placed in the "slots" where the abstract model expects.
The hope is that makes the material far less daunting, and helps it make sense in the mind of the reader.
