source common.sh

export FORCE_NIX_REMOTE=1

echo '*** testing slave mode ***'
clearStore
clearManifests
NIX_REMOTE=slave $SHELL ./user-envs.sh

echo '*** testing daemon mode ***'
clearStore
clearManifests
$nixworker --daemon &
pidDaemon=$!
trap "kill -9 $pidDaemon" EXIT
NIX_REMOTE=daemon $SHELL ./user-envs.sh
kill -9 $pidDaemon
wait $pidDaemon || true
trap "" EXIT
