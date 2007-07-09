#! /bin/sh -e

svnbin=$1
torevision=$2
repos=$3
statepath=$4

if [ "$#" != 4 ] ; then
  echo "Incorrect number of arguments"
  exit 1;
fi

$svnbin merge -r HEAD:$torevision $repos $statepath
