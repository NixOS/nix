---
synopsis: "Run the flake regressions test suite"
prs: 10603
---

This update introduces a GitHub action to run a subset of the [flake regressions test suite](https://github.com/NixOS/flake-regressions), which includes 259 flakes with their expected evaluation results. Currently, the action runs the first 25 flakes due to the full test suite's extensive runtime. A manually triggered action may be implemented later to run the entire test suite.

Author: [**Eelco Dolstra (@edolstra)**](https://github.com/edolstra)
