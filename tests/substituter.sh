#! /bin/sh -ex
echo $*

case $* in
    *)
        mkdir $1
        echo $3 $4 > $1/hello
        ;;
esac        

