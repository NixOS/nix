---
synopsis: "`nix repl` pretty-prints values"
prs: 9931
---

`nix repl` will now pretty-print values:

```
{
  attrs = {
    a = {
      b = {
        c = { };
      };
    };
  };
  list = [ 1 ];
  list' = [
    1
    2
    3
  ];
}
```
