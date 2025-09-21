let

  s = "${builtins.derivation {
    name = "test";
    builder = "/bin/sh";
    system = "x86_64-linux";
  }}";

  c = builtins.getContext s;

  matchRes = builtins.match ".*(-).*" s;

  splitRes = builtins.split "(-)" s;

in
[
  (c == builtins.getContext (builtins.head matchRes))
  (c == builtins.getContext (builtins.head splitRes))
  (c == builtins.getContext (builtins.head (builtins.elemAt splitRes 1)))
]
