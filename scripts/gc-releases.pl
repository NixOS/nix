#! /usr/bin/perl -w

use strict;
use readmanifest;


# Read the archive directories.
my @archives = ();
my %archives;

sub readDir {
    my $dir = shift;
    opendir(DIR, "$dir") or die "cannot open `$dir': $!";
    my @as = readdir DIR;
    foreach my $archive (@as) {
        push @archives, $archive;
        $archives{$archive} = "$dir/$archive";
    }
    closedir DIR;
}

readDir "/mnt/scratchy/eelco/public_html/nix-cache";
readDir "/mnt/scratchy/eelco/public_html/patches";

print STDERR scalar @archives, "\n";


# Read the manifests.
my %narFiles;
my %patches;
my %successors;

foreach my $manifest (@ARGV) {
    print STDERR "loading $manifest\n";
    if (readManifest($manifest, \%narFiles, \%patches, \%successors, 1) < 3) {
#        die "manifest `$manifest' is too old (i.e., for Nix <= 0.7)\n";
    }
}


# Find the live archives.
my %usedFiles;

foreach my $narFile (keys %narFiles) {
    foreach my $file (@{$narFiles{$narFile}}) {
        $file->{url} =~ /\/([^\/]+)$/;
        my $basename = $1;
        die unless defined $basename;
#        print $basename, "\n";
        $usedFiles{$basename} = 1;
        die "missing archive `$basename'"
            unless defined $archives{$basename};
    }
}

foreach my $patch (keys %patches) {
    foreach my $file (@{$patches{$patch}}) {
        $file->{url} =~ /\/([^\/]+)$/;
        my $basename = $1;
        die unless defined $basename;
#        print $basename, "\n";
        $usedFiles{$basename} = 1;
        die "missing archive `$basename'"
            unless defined $archives{$basename};
    }
}


# Print out the dead archives.
foreach my $archive (@archives) {
    next if $archive eq "." || $archive eq "..";
    if (!defined $usedFiles{$archive}) {
        print $archives{$archive}, "\n";
    }
}
