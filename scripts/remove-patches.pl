#! /usr/bin/perl -w -I/home/eelco/nix/scripts

use strict;
use readmanifest;

for my $p (@ARGV) {

    my %narFiles;
    my %patches;

    readManifest $p,
        \%narFiles, \%patches;

    %patches = ();
    
    writeManifest $p,
        \%narFiles, \%patches;
}
