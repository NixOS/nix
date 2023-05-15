# Release X.Y (202?-??-??)

- Speed-up of downloads from binary caches.
  The number of parallel downloads (also known as substitutions) has been separated from the [`--max-jobs` setting](../command-ref/conf-file.md#conf-max-jobs).
  The new setting is called [`max-substitution-jobs`](../command-ref/conf-file.md#conf-max-substitution-jobs).
  The number of parallel downloads is now set to 16 by default (previously, the default was 1 due to the coupling to build jobs).
