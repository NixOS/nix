outPath=$(readlink -f $TEST_ROOT/result)
grep 'FOO BAR BAZ' ${remoteDir}/${outPath}
