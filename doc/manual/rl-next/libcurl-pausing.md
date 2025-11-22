---
synopsis: Fix "download buffer is full; consider increasing the 'download-buffer-size' setting" warning
prs: [14614]
issues: [11728]
---

The underlying issue that led to [#11728](https://github.com/NixOS/nix/issues/11728) has been resolved by utilizing
[libcurl write pausing functionality](https://curl.se/libcurl/c/curl_easy_pause.html) to control backpressure when unpacking to slow destinations like the git-backed tarball cache. The default value of `download-buffer-size` is now 1 MiB and it's no longer recommended to increase it, since the root cause has been fixed.

This is expected to improve download performance on fast connections, since previously a single slow download consumer would stall the thread and prevent any other transfers from progressing.

Many thanks go out to the [Lix project](https://lix.systems/) for the [implementation](https://git.lix.systems/lix-project/lix/commit/4ae6fb5a8f0d456b8d2ba2aaca3712b4e49057fc) that served as inspiration for this change and for triaging libcurl [issues with pausing](https://github.com/curl/curl/issues/19334).
