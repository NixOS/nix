#! /bin/sh -e

if [ $(whoami) = "root" ]
then

su - wouterdb -c "cd /home/wouterdb/dev/nix-state/; make"
make install
chown -R wouterdb.wouterdb /nixstate2/nix/

./restartDaemon.sh

else
  echo "You must be ROOT to run this script."
  exit 0
fi

