#! /bin/sh -e

dir1=releases
dir2=nix-state
mkdir -p $dir1
cd $dir1
rm -rf $dir2
mkdir -p $dir2
cd $dir2
svn co https://svn.cs.uu.nl:12443/repos/trace/nix/branches/state ./
revision=`svn info | grep ^Revision | sed 's/Revision: //g'`
cd ..
date=`date +%Y%m%d`
tarfile=snix-${date}-rev${revision}.tar.gz
tar -cvf $tarfile \
    --preserve-permissions \
    --atime-preserve \
    --gzip \
    --verbose \
    --no-ignore-command-error \
    $dir2/
