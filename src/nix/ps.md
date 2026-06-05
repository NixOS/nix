R"(

# Examples

* Show all active builds:

  ```console
  # nix ps
  USER      PID         CPU  DERIVATION/COMMAND
  nixbld11  3534394  110.2s  /nix/store/lzvdxlbr6xjd9w8py4nd2y2nnqb9gz7p-nix-util-tests-3.13.2.drv (wall=8s)
  nixbld11  3534394    0.8s  └───bash -e /nix/store/jwqf79v5p51x9mv8vx20fv9mzm2x7kig-source-stdenv.sh /nix/store/shkw4qm9qcw5sc5n1k5jznc83ny02
  nixbld11  3534751   36.3s      └───ninja -j24
  nixbld11  3535637    0.0s          ├───/nix/store/0v2jfvx71l1zn14l97pznvbqnhiq3pyd-gcc-14.3.0/bin/g++ -fPIC -fstack-clash-protection -O2 -U_
  nixbld11  3535639    0.1s          │   └───/nix/store/0v2jfvx71l1zn14l97pznvbqnhiq3pyd-gcc-14.3.0/libexec/gcc/x86_64-unknown-linux-gnu/14.3.
  nixbld11  3535658    0.0s          └───/nix/store/0v2jfvx71l1zn14l97pznvbqnhiq3pyd-gcc-14.3.0/bin/g++ -fPIC -fstack-clash-protection -O2 -U_
  nixbld1   3534377    1.8s  /nix/store/nh2dx9cqcy9lw4d4rvd0dbsflwdsbzdy-patchelf-0.18.0.drv (wall=5s)
  nixbld1   3534377    1.8s  └───bash -e /nix/store/xk05lkk4ij6pc7anhdbr81appiqbcb01-default-builder.sh
  nixbld1   3535074    0.0s      └───/nix/store/21ymxxap3y8hb9ijcfah8ani9cjpv8m6-bash-5.2p37/bin/bash ./configure --disable-dependency-trackin
  ```

# Description

This command lists all currently running Nix builds.
For each build, it shows the derivation path and the main process ID.
On Linux and macOS, it also shows the child processes of each build.

)"
