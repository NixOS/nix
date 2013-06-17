#!/bin/sh

### BEGIN INIT INFO
# Provides:          nix-daemon
# Required-Start:    $local_fs $remote_fs $network $syslog
# Required-Stop:     $local_fs $remote_fs $network $syslog
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: starts the Nix daemon
# Description:       starts the Nix daemon start-stop-daemon
### END INIT INFO

PATH=/usr/local/sbin:/usr/local/bin:/sbin:/bin:/usr/sbin:/usr/bin
DAEMON=$(readlink -f /root/.nix-profile/bin/nix-daemon)
NAME=nix-daemon
DESC=nix-daemon

test -x $DAEMON || exit 0

set -e

if test -f /etc/default/nix-daemon; then
    . /etc/default/nix-daemon
fi

. /lib/lsb/init-functions

case "$1" in
	start)
		echo -n "Starting $DESC: "
		
		if test "$NIX_DISTRIBUTED_BUILDS" = "1"; then
		    NIX_BUILD_HOOK=$(dirname $DAEMON)/../libexec/nix/build-remote.pl
		
		    if test "$NIX_REMOTE_SYSTEMS" = "" ; then
			NIX_REMOTE_SYSTEMS=/etc/nix/remote-systems.conf
		    fi
		
		    # Set the current load facilities
		    NIX_CURRENT_LOAD=/var/run/nix/current-load
		
		    if test ! -d $NIX_CURRENT_LOAD; then
		        mkdir -p $NIX_CURRENT_LOAD
		    fi
		fi
		
		start-stop-daemon -b --start --quiet \
		    --exec /usr/bin/env \
		    NIX_REMOTE_SYSTEMS=$NIX_REMOTE_SYSTEMS \
		    NIX_BUILD_HOOK=$NIX_BUILD_HOOK \
		    NIX_CURRENT_LOAD=$NIX_CURRENT_LOAD \
		    $DAEMON -- $DAEMON_OPTS
		echo "$NAME."
		;;

	stop)
		echo -n "Stopping $DESC: "
		start-stop-daemon --stop --quiet --exec $DAEMON
		echo "$NAME."
		;;

	restart|force-reload)
		echo -n "Restarting $DESC: "
		start-stop-daemon --stop --quiet --exec $DAEMON
		sleep 1
		start-stop-daemon --start --quiet --exec $DAEMON -- $DAEMON_OPTS
		echo "$NAME."
		;;

	reload)
		echo -n "Reloading $DESC configuration: "
		start-stop-daemon --stop --signal HUP --quiet --exec $DAEMON
		echo "$NAME."
		;;

	status)
		status_of_proc "$DAEMON" nix-daemon && exit 0 || exit $?
		;;
	*)
		echo "Usage: $NAME {start|stop|restart|reload|force-reload|status}" >&2
		exit 1
		;;
esac

exit 0
