#! /usr/bin/perl -w -I/home/eelco/nix/scripts

use strict;
use readmanifest;

for my $p (@ARGV) {

    my %narFiles;
    my %localPaths;
    my %patches;

    readManifest $p, \%narFiles, \%localPaths, \%patches;

    %patches = ();
    
    writeManifest $p, \%narFiles, \%patches;
}
