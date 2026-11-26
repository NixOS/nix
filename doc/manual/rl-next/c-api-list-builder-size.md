---
synopsis: "C API: the list builder size is the exact list length"
packages: [c-api]
prs: [16906]
---

Since the C API was introduced in Nix 2.22, `nix_make_list` has created a list whose length is exactly the number given to `nix_make_list_builder`.
The documentation called that parameter `capacity` and described it as an upper bound ("Don't exceed"), but this is not how the implementation behaved, as unset items crashed the evaluator.

The parameter is now called `size`, and the documentation states the [actual contract].

[actual contract]: https://hydra.nixos.org/job/nix/master/external-api-docs/latest/download-by-type/doc/external-api-docs/structListBuilder.html
