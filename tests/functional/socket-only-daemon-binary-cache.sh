#!/usr/bin/env bash

# Run the socket-only-daemon test with a binary cache backend
# This tests that
# - The daemon can serve a binary cache store; not just a local store
# - Client's store operations do not reach into the file system secretly,
#   because the files don't exist in the places where a local store would
#   put them. (We have NARs instead)

daemon_backing_store_is_binary_cache=1
source socket-only-daemon.sh
