---
synopsis: "Rename `nix profile install` to `nix profile add`"
prs: [13224]
---

The command `nix profile install` has been renamed to `nix profile add` (though the former is still available as an alias). This is because the verb "add" is a better antonym for the verb "remove" (i.e. `nix profile remove`). Nix also does not have install hooks or general behavior often associated with "installing".
