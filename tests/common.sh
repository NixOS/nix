#set -e

cecho() {
  local code='\033['
  case "$1" in
    black  | bk) color="${code}0;30m" ;;
    red    |  r) color="${code}1;31m" ;;
    green  |  g) color="${code}1;32m" ;;
    yellow |  y) color="${code}1;33m" ;;
    blue   |  b) color="${code}1;34m" ;;
    purple |  p) color="${code}1;35m" ;;
    cyan   |  c) color="${code}1;36m" ;;
    gray   | gr) color="${code}0;37m" ;;
    *) local text="$1"
  esac
  [ -z "$text" ] && local text="${color}$2${code}0m"
  echo -e "$text"
}

trap 'traperror $? $LINENO $BASH_LINENO "$BASH_COMMAND" $(printf "::%s" ${FUNCNAME[@]})'  ERR

traperror () {
    local err=$1 # error status
    local line=$2 # LINENO
    local linecallfunc=$3
    local command="$4"
    local funcstack="$5"
    cecho red "ERROR: line $line - command '$command' exited with status: $err"
    if [ "$funcstack" != "::" ]; then
        cecho red -n "   ... Error at ${funcstack} "
        if [ "$linecallfunc" != "" ]; then
            cecho red -n "called at line $linecallfunc"
        fi
        else
            cecho red -n "   ... internal debug info from function ${FUNCNAME} (line $linecallfunc)"
    fi
    echo
}

initNewTest()
{
    export TEST_ROOT=$(mktemp -d -p $(pwd))

    export NIX_STORE_DIR
    if ! NIX_STORE_DIR=$(readlink -f $TEST_ROOT/store 2> /dev/null); then
        # Maybe the build directory is symlinked.
        export NIX_IGNORE_SYMLINK_STORE=1
        NIX_STORE_DIR=$TEST_ROOT/store
    fi
    export NIX_LOCALSTATE_DIR=$TEST_ROOT/var
    export        NIX_LOG_DIR=$TEST_ROOT/var/log/nix
    export      NIX_STATE_DIR=$TEST_ROOT/var/nix
    export       NIX_CONF_DIR=$TEST_ROOT/etc
    export  NIX_MANIFESTS_DIR=$TEST_ROOT/var/nix/manifests
    export   _NIX_TEST_SHARED=$TEST_ROOT/shared

    mkdir -p $TEST_ROOT/{store,var/log/nix,var/nix/manifests,etc,shared,test-home}

    if [[ -n $NIX_STORE ]]; then
        export _NIX_TEST_NO_SANDBOX=1
    fi
    export _NIX_IN_TEST=$TEST_ROOT/shared
    export   NIX_REMOTE=$NIX_REMOTE_
    unset NIX_PATH
    export TEST_HOME=$TEST_ROOT/test-home
    [ ! -v OLD_HOME ] && OLD_HOME=$HOME
    export HOME=$TEST_HOME

    export NIX_BUILD_HOOK=

    cat > "$NIX_CONF_DIR"/nix.conf <<-EOF
		build-users-group =
		gc-keep-outputs = false
		gc-keep-derivations = false
		env-keep-derivations = false
		fsync-metadata = false
EOF

    # Initialise the database.
    nix-store --init

    # Did anything happen?
    test -e "$NIX_STATE_DIR"/db/db.sqlite
}

setupTest()
{
    cecho red "running setupTest"
    if [ -z $TEST_ROOT ]; then
        initNewTest
        trap "destroyTest" EXIT
    fi
}

destroyTest()
{
    cecho red "running destroyTest"
    chmod -R +w "$NIX_STORE_DIR"
    rm -rf $TEST_ROOT

    unset TEST_ROOT
    unset NIX_STORE_DIR
    unset NIX_IGNORE_SYMLINK_STORE
    unset NIX_LOCALSTATE_DIR
    unset NIX_LOG_DIR
    unset NIX_STATE_DIR
    unset NIX_CONF_DIR
    unset NIX_MANIFESTS_DIR
    unset _NIX_TEST_SHARED
    unset NIX_STORE
    unset _NIX_TEST_NO_SANDBOX
    unset _NIX_IN_TEST
    unset NIX_REMOTE

    unset TEST_HOME
    if [ -v OLD_HOME ]; then
        HOME=$OLD_HOME
        unset OLD_HOME
    fi
}

readLink() {
    ls -l "$1" | sed 's/.*->\ //'
}

clearProfiles() {
    profiles="$NIX_STATE_DIR"/profiles
    rm -rf $profiles
}

clearStore() {
    echo "clearing store..."
    chmod -R +w "$NIX_STORE_DIR"
    rm -rf "$NIX_STORE_DIR"
    mkdir "$NIX_STORE_DIR"
    rm -rf "$NIX_STATE_DIR"
    mkdir "$NIX_STATE_DIR"
    nix-store --init
    clearProfiles
}

clearCache() {
    rm -rf "$cacheDir"
}

clearCacheCache() {
    rm -f $TEST_HOME/.cache/nix/binary-cache*
}

startDaemon() {
    # Start the daemon, wait for the socket to appear.  !!!
    # ‘nix-daemon’ should have an option to fork into the background.
    rm -f $NIX_STATE_DIR/daemon-socket/socket
    nix-daemon &
    for ((i = 0; i < 30; i++)); do
        if [ -e $NIX_STATE_DIR/daemon-socket/socket ]; then break; fi
        sleep 1
    done
    pidDaemon=$!
    trap "kill -9 $pidDaemon" EXIT
    export NIX_REMOTE=daemon
}

killDaemon() {
    kill -9 $pidDaemon
    wait $pidDaemon || true
    trap "" EXIT
}

fail() {
    echo "$1"
    exit 1
}

#set -x
