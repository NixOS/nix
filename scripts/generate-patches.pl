#! /usr/bin/perl -w -I/home/eelco/nix/scripts

use strict;
use POSIX qw(tmpnam);
use readmanifest;

die unless scalar @ARGV == 5;

my $cacheDir = $ARGV[0];
my $patchesDir = $ARGV[1];
my $patchesURL = $ARGV[2];
my $srcDir = $ARGV[3];
my $dstDir = $ARGV[4];

my $tmpdir;
do { $tmpdir = tmpnam(); }
until mkdir $tmpdir, 0777;

print "TEMP = $tmpdir\n";

#END { rmdir $tmpdir; }

my %srcNarFiles;
my %srcPatches;
my %srcSuccessors;

my %dstNarFiles;
my %dstPatches;
my %dstSuccessors;

readManifest "$srcDir/MANIFEST",
    \%srcNarFiles, \%srcPatches, \%srcSuccessors;

readManifest "$dstDir/MANIFEST",
    \%dstNarFiles, \%dstPatches, \%dstSuccessors;


sub findOutputPaths {
    my $narFiles = shift;
    my $successors = shift;

    my %outPaths;
    
    foreach my $p (keys %{$narFiles}) {

        # Ignore store expressions.
        next if ($p =~ /\.store$/);
        
        # Ignore builders (too much ambiguity -- they're all called
        # `builder.sh').
        next if ($p =~ /\.sh$/);
        next if ($p =~ /\.patch$/);
        
        # Don't bother including tar files etc.
        next if ($p =~ /\.tar\.(gz|bz2)$/ || $p =~ /\.zip$/ || $p =~ /\.bin$/);

        $outPaths{$p} = 1;
    }

    return %outPaths;
}

print "finding src output paths...\n";
my %srcOutPaths = findOutputPaths \%srcNarFiles, \%srcSuccessors;

print "finding dst output paths...\n";
my %dstOutPaths = findOutputPaths \%dstNarFiles, \%dstSuccessors;


sub getNameVersion {
    my $p = shift;
    $p =~ /\/[0-9a-f]+((?:-[a-zA-Z][^\/-]*)+)([^\/]*)$/;
    my $name = $1;
    my $version = $2;
    $name =~ s/^-//;
    $version =~ s/^-//;
    return ($name, $version);
}


# A quick hack to get a measure of the `distance' between two
# versions: it's just the position of the first character that differs
# (or 999 if they are the same).
sub versionDiff {
    my $s = shift;
    my $t = shift;
    my $i;
    return 999 if $s eq $t;
    for ($i = 0; $i < length $s; $i++) {
        return $i if $i >= length $t or
            substr($s, $i, 1) ne substr($t, $i, 1);
    }
    return $i;
}


sub getNarBz2 {
    my $narFiles = shift;
    my $storePath = shift;
    
    my $narFileList = $$narFiles{$storePath};
    die "missing store expression $storePath" unless defined $narFileList;

    my $narFile = @{$narFileList}[0];
    die unless defined $narFile;

    $narFile->{url} =~ /\/([^\/]+)$/;
    die unless defined $1;
    return "$cacheDir/$1";
}


sub containsPatch {
    my $patches = shift;
    my $storePath = shift;
    my $basePath = shift;
    my $patchList = $$patches{$storePath};
    return 0 if !defined $patchList;
    my $found = 0;
    foreach my $patch (@{$patchList}) {
        # !!! baseHash might differ
        return 1 if $patch->{basePath} eq $basePath;
    }
    return 0;
}


# For each output path in the destination, see if we need to / can
# create a patch.

print "creating patches...\n";

foreach my $p (keys %dstOutPaths) {

    # If exactly the same path already exists in the source, skip it.
    next if defined $srcOutPaths{$p};
    
#    print "  $p\n";

    # If not, then we should find the path in the source that is
    # `most' likely to be present on a system that wants to install
    # this path.

    (my $name, my $version) = getNameVersion $p;

    my @closest = ();
    my $closestVersion;
    my $minDist = -1; # actually, larger means closer

    # Find all source paths with the same name.

    foreach my $q (keys %srcOutPaths) {
        (my $name2, my $version2) = getNameVersion $q;
        if ($name eq $name2) {
            my $dist = versionDiff $version, $version2;
            if ($dist > $minDist) {
                $minDist = $dist;
                @closest = ($q);
                $closestVersion = $version2;
            } elsif ($dist == $minDist) {
                push @closest, $q;
            }
        }
    }

    if (scalar(@closest) == 0) {
        print "  NO BASE: $p\n";
        next;
    }

    foreach my $closest (@closest) {

        # Generate a patch between $closest and $p.
        print "  $p <- $closest\n";

        # If the patch already exists, skip it.
        if (containsPatch(\%srcPatches, $p, $closest) ||
            containsPatch(\%dstPatches, $p, $closest))
        {
            print "    skipping, already exists\n";
            next;
        }

#        next;
        
        my $srcNarBz2 = getNarBz2 \%srcNarFiles, $closest;
        my $dstNarBz2 = getNarBz2 \%dstNarFiles, $p;
        
        system("bunzip2 < $srcNarBz2 > $tmpdir/A") == 0
            or die "cannot unpack $srcNarBz2";

        system("bunzip2 < $dstNarBz2 > $tmpdir/B") == 0
            or die "cannot unpack $dstNarBz2";

        system("bsdiff $tmpdir/A $tmpdir/B $tmpdir/DIFF") == 0
            or die "cannot compute binary diff";

        my $baseHash = `nix-hash --flat $tmpdir/A` or die;
        chomp $baseHash;

        my $narHash = `nix-hash --flat $tmpdir/B` or die;
        chomp $narHash;

        my $narDiffHash = `nix-hash --flat $tmpdir/DIFF` or die;
        chomp $narDiffHash;

        my $narDiffSize = (stat "$tmpdir/DIFF")[7];
        my $dstNarBz2Size = (stat $dstNarBz2)[7];

        if ($narDiffSize >= $dstNarBz2Size) {
            print "    rejecting; patch bigger than full archive\n";
            next;
        }
    
        my $finalName =
            "$narDiffHash-$name-$closestVersion-to-$version.nar-bsdiff";

        if (-e "$patchesDir/$finalName") {
            print "    not copying, already exists\n";
            next;
        }

        print "    size $narDiffSize; full size $dstNarBz2Size\n";
        
        system("cp '$tmpdir/DIFF' '$patchesDir/$finalName.tmp'") == 0
            or die "cannot copy diff";

        rename("$patchesDir/$finalName.tmp", "$patchesDir/$finalName")
            or die "cannot rename $patchesDir/$finalName.tmp";
        
        # Add the patch to the manifest.
        addPatch \%dstPatches, $p,
            { url => "$patchesURL/$finalName", hash => $narDiffHash
            , size => $narDiffSize
            , basePath => $closest, baseHash => $baseHash
            , narHash => $narHash, patchType => "nar-bsdiff"
            };
    }
}


# Add in any potentially useful patches in the source (namely, those
# patches that produce either paths in the destination or paths that
# can be used as the base for other useful patches).

my $changed;
do {
    # !!! we repeat this to reach the transitive closure; inefficient
    $changed = 0;

    foreach my $p (keys %srcPatches) {
        my $patchList = $srcPatches{$p};

        my $include = 0;

        # Is path $p included in the destination?  If so, include
        # patches that produce it.
        $include = 1 if (defined $dstNarFiles{$p});

        # Is path $p a path that serves as a base for paths in the
        # destination?  If so, include patches that produce it.
        foreach my $q (keys %dstPatches) {
            foreach my $patch (@{$dstPatches{$q}}) {
                # !!! check baseHash
                $include = 1 if ($p eq $patch->{basePath});
            }
        }

        if ($include) {
            foreach my $patch (@{$patchList}) {
                $changed = 1 if addPatch \%dstPatches, $p, $patch;
            }
        }            
        
    }
    
} while $changed;


# Rewrite the manifest of the destination (with the new patches).
writeManifest "$dstDir/MANIFEST",
    \%dstNarFiles, \%dstPatches, \%dstSuccessors;
