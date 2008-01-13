#! /bin/sh -e

make clean				# comment this out when needed !!!

export nixstatepath=/nixstate2/nix
export ACLOCAL_PATH=/home/wouterdb/.nix-profile/share/aclocal

if [ "$1" = "full" ]; then
  nix-env-all-pkgs.sh -i gcc
  nix-env-all-pkgs.sh -i gnum4
  nix-env-all-pkgs.sh -i autoconf
  nix-env-all-pkgs.sh -i automake
  nix-env-all-pkgs.sh -i gnused
  nix-env-all-pkgs.sh -i db4
  nix-env-all-pkgs.sh -i aterm
  nix-env-all-pkgs.sh -i bzip2
  nix-env-all-pkgs.sh -i flex
  nix-env-all-pkgs.sh -i bsdiff
  nix-env-all-pkgs.sh -i libtool
  nix-env-all-pkgs.sh -i docbook5
  nix-env-all-pkgs.sh -i docbook5-xsl
  nix-env-all-pkgs.sh -i bison
  nix-env-all-pkgs.sh -i gdb			#optional for debugging
  nix-env-all-pkgs.sh -i gnupatch
  nix-env-all-pkgs.sh -i gnumake
  nix-env-all-pkgs.sh -i ext3cow-tools
  nix-env-all-pkgs.sh -i e3cfsprogs
  nix-env-all-pkgs.sh -i rsync  
fi

if [ "$1" = "full" ] || [ "$1" = "auto" ]; then
  export AUTOCONF=autoconf
  export AUTOHEADER=autoheader
  export AUTOMAKE=automake
  autoconf
  autoreconf -f
  aclocal
  autoheader
  automake
fi

./bootstrap.sh

./configure --with-aterm=$HOME/.nix-profile \
            --with-bzip2=$HOME/.nix-profile \
            --with-bdb=$HOME/.nix-profile \
            --with-docbook-xsl=$HOME/.nix-profile \
            --with-docbook-rng=/home/wouterdb/.nix-profile/xml/rng/docbook \
            --with-docbook-xsl=/home/wouterdb/.nix-profile/xml/xsl/docbook \
            --prefix=$nixstatepath \
            --with-store-dir=/nix/store \
	    	--with-store-state-dir=/nix/state \
            --with-ext3cow-header=/nix/store/v95qf520d6972pshykrah1dz3z53rkmj-linux-2.6.21.7/lib/modules/2.6.21.7-default/build/include/linux/ext3cow_fs.h \
            --localstatedir=/nix/var

#Options from the nix expr
#--disable-init-state
#--with-store-dir=/nix/store 
#--localstatedir=/nix/var
#--with-aterm=/nix/store/pkmzbb613wa8cwngx8jjb5jaic8yhyzs-aterm-2.4.2-fixes
#--with-bdb=/nix/store/4yv4j1cd7i5j3mhs5wpc1kzlz1cj8n82-db4-4.5.20
#--with-bzip2=/nix/store/dh0mdgkvhv3pwrf8zp58phpzn9rcm49r-bzip2-1.0.3
#--disable-init-state


echo "New state nix version by wouter ..." > doc/manual/NEWS.txt
make
