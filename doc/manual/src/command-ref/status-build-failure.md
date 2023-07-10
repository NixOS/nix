# Special exit codes for build failure

1xx status codes are used when requested builds failed.
The following codes are in use:

- `100` Generic build failure

  The builder process returned with a non-zero exit code.

- `101` Build timeout

  The build was aborted because it did not complete within the specified `timeout`.

- `102` Hash mismatch

  The build output was rejected because it does not match the
  [`outputHash` attribute of the derivation](@docroot@/language/advanced-attributes.md).

- `104` Not deterministic

  The build succeeded in check mode but the resulting output is not binary reproducible.

With the `--keep-going` flag it's possible for multiple failures to occur.
In this case the 1xx status codes are or combined using
[bitwise OR](https://en.wikipedia.org/wiki/Bitwise_operation#OR).

```
0b1100100
     ^^^^
     |||`- timeout
     ||`-- output hash mismatch
     |`--- build failure
     `---- not deterministic
```
