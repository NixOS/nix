#! /bin/sh -e

svnbin=$1
torevision=$2
repos=$3

if [ "$#" != 3 ] ; then
  echo "Incorrect number of arguments"
  exit 1;
fi

$svnbin merge -r HEAD:$2 $3

