#! /usr/bin/perl -w -I/home/eelco/nix/scripts

use strict;
use readmanifest;

for my $p (@ARGV) {

    my %narFiles;
    my %patches;
    my %successors;

    readManifest $p,
        \%narFiles, \%patches, \%successors;

    %patches = ();
    
    writeManifest $p,
        \%narFiles, \%patches, \%successors;
}
