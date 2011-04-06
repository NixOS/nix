#! /usr/bin/perl -w -I.

use strict;
use readmanifest;

die unless scalar @ARGV == 2;

my $cache = $ARGV[0];
my $manifest = $ARGV[1];
my %narFiles;
my %patches;

readManifest $manifest, \%narFiles, \%patches;

foreach my $storePath (keys %narFiles) {
    my $narFileList = $narFiles{$storePath};

    foreach my $narFile (@{$narFileList}) {
        if (!defined $narFile->{size} or
            !defined $narFile->{narHash})
        {
            $narFile->{url} =~ /\/([^\/]+)$/;
            die unless defined $1;
            my $fn = "$cache/$1";
            
            my @info = stat $fn or die;
            $narFile->{size} = $info[7];

            my $narHash;
            my $hashFile = "$fn.NARHASH";
            if (-e $hashFile) {
                open HASH, "<$hashFile" or die;
                $narHash = <HASH>;
                close HASH;
            } else {
                print "$fn\n";
                $narHash = `bunzip2 < '$fn' | nix-hash --flat /dev/stdin` or die;
                open HASH, ">$hashFile" or die;
                print HASH $narHash;
                close HASH;
            }
            chomp $narHash;
            $narFile->{narHash} = $narHash;
        }
    }
}

if (! -e "$manifest.backup") {
    system "mv --reply=no '$manifest' '$manifest.backup'";
}

writeManifest $manifest, \%narFiles, \%patches;
