#!/usr/bin/env bash

hashAlgo=sha1

source simple-common.sh

initRepo

# blob
test0
try "557db03de997c86a4a028e1ebd3a1ceb225be238"

# tree with children
test1
try "e5c0a11a556801a5c9dcf330ca9d7e2c572697f4"

test2
try2 dummy1 "980a0d5f19a64b4b30a87d4206aade58726b60e3"

test3
try2 dummy2 "8b8e43b937854f4083ea56777821abda2799e850"

test4
try2 dummy3 "f227adfaf60d2778aabbf93df6dd061272d2dc85"

test5
try2 dummy4 "06f3e789820fc488d602358f03e3a1cbf993bf33"
