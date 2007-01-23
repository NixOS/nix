#! /usr/bin/perl -w -I. -I..

use strict;
use readmanifest;
use readcache;


my %allNarFiles;
my %allLocalPaths;
my %allPatches;

foreach my $manifest (glob "/data/webserver/dist/*/*/MANIFEST") {
    print STDERR "loading $manifest\n";
    readManifest($manifest, \%allNarFiles, \%allLocalPaths, \%allPatches, 1);
}



foreach my $manifest (@ARGV) {

    print STDERR "shrinking manifest $manifest...\n";

    my %narFiles;
    my %patches;

    if (readManifest($manifest, \%narFiles, \%patches, 1) < 3) {
        print STDERR "manifest `$manifest' is too old (i.e., for Nix <= 0.7)\n";
	next;
    }

    my %done;

    sub traverse {
	my $p = shift;
	my $prefix = shift;
	print "$prefix$p\n";

	my $reachesNAR = 0;

	foreach my $patch (@{$patches{$p}}) {
	    next if defined $done{$patch->{url}};
	    $done{$patch->{url}} = 1;
	    $reachesNAR = 1 if traverse ($patch->{basePath}, $prefix . "  ");
	}

	$reachesNAR = 1 if defined $allNarFiles{$p};

	print "  $prefix$reachesNAR\n";
	return $reachesNAR;
    }

#    foreach my $p (keys %narFiles) {
#	traverse ($p, "");
#    }

    my %newPatches;

    foreach my $p (keys %patches) {
	my $patchList = $patches{$p};
	my @newList;
	foreach my $patch (@{$patchList}) {
	    if (! defined $allNarFiles{$patch->{basePath}} || 
		! defined $allNarFiles{$p} ) 
	    {
#		print "REMOVING PATCH ", $patch->{basePath}, " -> ", $p, "\n";
	    } else {
#		print "KEEPING PATCH ", $patch->{basePath}, " -> ", $p, "\n";
		push @newList, $patch;
	    }
	}
	$newPatches{$p} = \@newList;
    }

    writeManifest ($manifest, \%narFiles, \%newPatches);
}

