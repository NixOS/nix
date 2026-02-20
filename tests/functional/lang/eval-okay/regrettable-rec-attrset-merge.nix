# This is for backwards compatibility, not because we like it.
# See https://github.com/NixOS/nix/issues/9020.
{
  a = rec {
    b = c + 1;
    d = 2;
  };
  a.c = d + 3;
}
.a.b
