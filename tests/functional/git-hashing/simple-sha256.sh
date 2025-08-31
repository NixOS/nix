#!/usr/bin/env bash

hashAlgo=sha256

source simple-common.sh

requireDaemonNewerThan 2.31pre20250724

initRepo

# blob
test0
try "7c5c8610459154bdde4984be72c48fb5d9c1c4ac793a6b5976fe38fd1b0b1284"

# tree with children
test1
try "cd79952f42462467d0ea574b0283bb6eb77e15b2b86891e29f2b981650365474"

test2
try2 dummy1 "f5b5cec05fb6f9302b507a48c1573e6f36075e954d97caa8667f784e9cdb0d13"

test3
try2 dummy2 "399d851c74ceac2c2b61b53b13dcf5e88df3b6135c7df1f248a323c3c2f9aa78"

test4
try2 dummy3 "d3ae8fc87e76b9b871bd06a58c925c5fb5f83b5393f9f58e4f6dba3f59470289"

test5
try2 dummy4 "8c090dd057e8e01ffe1fec24a3133dfe52ba4eda822e67ee7fefc2af7c6a2906"
