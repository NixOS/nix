#! /bin/sh -e

svnbin=$1
repos=$2

if [ "$#" != 2 ] ; then
  echo "Incorrect number of arguments"
  exit 1;
fi

$svnbin info $repos | sed -n '/^Revision: /p' | sed 's/Revision: //' 

# | tr -d "\12"
