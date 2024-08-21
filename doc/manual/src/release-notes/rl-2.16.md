# Release 2.16 (2023-05-31)

* Speed-up of downloads from binary caches.
  The number of parallel downloads (also known as substitutions) has been separated from the [`--max-jobs` setting](../command-ref/conf-file.md#conf-max-jobs).
  The new setting is called [`max-substitution-jobs`](../command-ref/conf-file.md#conf-max-substitution-jobs).
  The number of parallel downloads is now set to 16 by default (previously, the default was 1 due to the coupling to build jobs).

* The function [`builtins.replaceStrings`](@docroot@/language/builtins.md#builtins-replaceStrings) is now lazy in the value of its second argument `to`. That is, `to` is only evaluated when its corresponding pattern in `from` is matched in the string `s`.
