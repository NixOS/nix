# Tuning Cores and Jobs

Nix has two relevant settings with regards to how your CPU cores will be
utilized: [???](#conf-cores) and [???](#conf-max-jobs). This chapter
will talk about what they are, how they interact, and their
configuration trade-offs.

  - [???](#conf-max-jobs)  
    Dictates how many separate derivations will be built at the same
    time. If you set this to zero, the local machine will do no builds.
    Nix will still substitute from binary caches, and build remotely if
    remote builders are configured.

  - [???](#conf-cores)  
    Suggests how many cores each derivation should use. Similar to `make
    -j`.

The [???](#conf-cores) setting determines the value of
NIX\_BUILD\_CORES. NIX\_BUILD\_CORES is equal to [???](#conf-cores),
unless [???](#conf-cores) equals `0`, in which case NIX\_BUILD\_CORES
will be the total number of cores in the system.

The maximum number of consumed cores is a simple multiplication,
[???](#conf-max-jobs) \* NIX\_BUILD\_CORES.

The balance on how to set these two independent variables depends upon
each builder's workload and hardware. Here are a few example scenarios
on a machine with 24 cores:

| [???](#conf-max-jobs) | [???](#conf-cores) | NIX\_BUILD\_CORES | Maximum Processes | Result                                                                                                                                                                                                                                                                                 |
| --------------------- | ------------------ | ----------------- | ----------------- | -------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| 1                     | 24                 | 24                | 24                | One derivation will be built at a time, each one can use 24 cores. Undersold if a job can’t use 24 cores.                                                                                                                                                                              |
| 4                     | 6                  | 6                 | 24                | Four derivations will be built at once, each given access to six cores.                                                                                                                                                                                                                |
| 12                    | 6                  | 6                 | 72                | 12 derivations will be built at once, each given access to six cores. This configuration is over-sold. If all 12 derivations being built simultaneously try to use all six cores, the machine's performance will be degraded due to extensive context switching between the 12 builds. |
| 24                    | 1                  | 1                 | 24                | 24 derivations can build at the same time, each using a single core. Never oversold, but derivations which require many cores will be very slow to compile.                                                                                                                            |
| 24                    | 0                  | 24                | 576               | 24 derivations can build at the same time, each using all the available cores of the machine. Very likely to be oversold, and very likely to suffer context switches.                                                                                                                  |

Balancing 24 Build Cores

It is up to the derivations' build script to respect host's requested
cores-per-build by following the value of the NIX\_BUILD\_CORES
environment variable.
