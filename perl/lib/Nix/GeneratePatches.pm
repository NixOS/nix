package Nix::GeneratePatches;

use strict;
use File::Temp qw(tempdir);
use File::stat;
use Nix::Config;
use Nix::Manifest;

our @ISA = qw(Exporter);
our @EXPORT = qw(generatePatches propagatePatches copyPatches);


# Some patch generations options.

# Max size of NAR archives to generate patches for.
my $maxNarSize = $ENV{"NIX_MAX_NAR_SIZE"};
$maxNarSize = 160 * 1024 * 1024 if !defined $maxNarSize;

# If patch is bigger than this fraction of full archive, reject.
my $maxPatchFraction = $ENV{"NIX_PATCH_FRACTION"};
$maxPatchFraction = 0.60 if !defined $maxPatchFraction;

my $timeLimit = $ENV{"NIX_BSDIFF_TIME_LIMIT"};
$timeLimit = 180 if !defined $timeLimit;

my $hashAlgo = "sha256";


sub findOutputPaths {
    my $narFiles = shift;

    my %outPaths;
    
    foreach my $p (keys %{$narFiles}) {

        # Ignore derivations.
        next if ($p =~ /\.drv$/);
        
        # Ignore builders (too much ambiguity -- they're all called
        # `builder.sh').
        next if ($p =~ /\.sh$/);
        next if ($p =~ /\.patch$/);
        
        # Don't bother including tar files etc.
        next if ($p =~ /\.tar$/ || $p =~ /\.tar\.(gz|bz2|Z|lzma|xz)$/ || $p =~ /\.zip$/ || $p =~ /\.bin$/ || $p =~ /\.tgz$/ || $p =~ /\.rpm$/ || $p =~ /cvs-export$/ || $p =~ /fetchhg$/);

        $outPaths{$p} = 1;
    }

    return %outPaths;
}


sub getNameVersion {
    my $p = shift;
    $p =~ /\/[0-9a-z]+((?:-[a-zA-Z][^\/-]*)+)([^\/]*)$/;
    my $name = $1;
    my $version = $2;
    return undef unless defined $name && defined $version;
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
    my $narPath = shift;
    my $narFiles = shift;
    my $storePath = shift;
    
    my $narFileList = $$narFiles{$storePath};
    die "missing path $storePath" unless defined $narFileList;

    my $narFile = @{$narFileList}[0];
    die unless defined $narFile;

    $narFile->{url} =~ /\/([^\/]+)$/;
    die unless defined $1;
    return "$narPath/$1";
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


sub generatePatches {
    my ($srcNarFiles, $dstNarFiles, $srcPatches, $dstPatches, $narPath, $patchesPath, $patchesURL, $tmpDir) = @_;

    my %srcOutPaths = findOutputPaths $srcNarFiles;
    my %dstOutPaths = findOutputPaths $dstNarFiles;

    # For each output path in the destination, see if we need to / can
    # create a patch.

    print STDERR "creating patches...\n";

    foreach my $p (keys %dstOutPaths) {

        # If exactly the same path already exists in the source, skip it.
        next if defined $srcOutPaths{$p};
    
        print "  $p\n";

        # If not, then we should find the paths in the source that are
        # `most' likely to be present on a system that wants to
        # install this path.

        (my $name, my $version) = getNameVersion $p;
        next unless defined $name && defined $version;

        my @closest = ();
        my $closestVersion;
        my $minDist = -1; # actually, larger means closer

        # Find all source paths with the same name.

        foreach my $q (keys %srcOutPaths) {
            (my $name2, my $version2) = getNameVersion $q;
            next unless defined $name2 && defined $version2;

            if ($name eq $name2) {

                my $srcSystem = @{$$dstNarFiles{$p}}[0]->{system};
                my $dstSystem = @{$$srcNarFiles{$q}}[0]->{system};
                if (defined $srcSystem && defined $dstSystem && $srcSystem ne $dstSystem) {
                    print "    SKIPPING $q due to different systems ($srcSystem vs. $dstSystem)\n";
                    next;
                }

                # If the sizes differ too much, then skip.  This
                # disambiguates between, e.g., a real component and a
                # wrapper component (cf. Firefox in Nixpkgs).
                my $srcSize = @{$$srcNarFiles{$q}}[0]->{size};
                my $dstSize = @{$$dstNarFiles{$p}}[0]->{size};
                my $ratio = $srcSize / $dstSize;
                $ratio = 1 / $ratio if $ratio < 1;
                # print "  SIZE $srcSize $dstSize $ratio $q\n";

                if ($ratio >= 3) {
                    print "    SKIPPING $q due to size ratio $ratio ($srcSize vs. $dstSize)\n";
                    next;
                }

                # If there are multiple matching names, include the
                # ones with the closest version numbers.
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
            print "    NO BASE: $p\n";
            next;
        }

        foreach my $closest (@closest) {

            # Generate a patch between $closest and $p.
            print STDERR "  $p <- $closest\n";

            # If the patch already exists, skip it.
            if (containsPatch($srcPatches, $p, $closest) ||
                containsPatch($dstPatches, $p, $closest))
            {
                print "    skipping, already exists\n";
                next;
            }

            my $srcNarBz2 = getNarBz2 $narPath, $srcNarFiles, $closest;
            my $dstNarBz2 = getNarBz2 $narPath, $dstNarFiles, $p;

            if (! -f $srcNarBz2) {
                warn "patch source archive $srcNarBz2 is missing\n";
                next;
            }

            system("$Nix::Config::bzip2 -d < $srcNarBz2 > $tmpDir/A") == 0
                or die "cannot unpack $srcNarBz2";

            if (stat("$tmpDir/A")->size >= $maxNarSize) {
                print "    skipping, source is too large\n";
                next;
            }
        
            system("$Nix::Config::bzip2 -d < $dstNarBz2 > $tmpDir/B") == 0
                or die "cannot unpack $dstNarBz2";

            if (stat("$tmpDir/B")->size >= $maxNarSize) {
                print "    skipping, destination is too large\n";
                next;
            }
        
            my $time1 = time();
            my $res = system("ulimit -t $timeLimit; $Nix::Config::libexecDir/nix/bsdiff $tmpDir/A $tmpDir/B $tmpDir/DIFF");
            my $time2 = time();
            if ($res) {
                warn "binary diff computation aborted after ", $time2 - $time1, " seconds\n";
                next;
            }

            my $baseHash = `$Nix::Config::binDir/nix-hash --flat --type $hashAlgo --base32 $tmpDir/A` or die;
            chomp $baseHash;

            my $narHash = `$Nix::Config::binDir/nix-hash --flat --type $hashAlgo --base32 $tmpDir/B` or die;
            chomp $narHash;

            my $narDiffHash = `$Nix::Config::binDir/nix-hash --flat --type $hashAlgo --base32 $tmpDir/DIFF` or die;
            chomp $narDiffHash;

            my $narDiffSize = stat("$tmpDir/DIFF")->size;
            my $dstNarBz2Size = stat($dstNarBz2)->size;

            print "    size $narDiffSize; full size $dstNarBz2Size; ", $time2 - $time1, " seconds\n";
        
            if ($narDiffSize >= $dstNarBz2Size) {
                print "    rejecting; patch bigger than full archive\n";
                next;
            }
    
            if ($narDiffSize / $dstNarBz2Size >= $maxPatchFraction) {
                print "    rejecting; patch too large relative to full archive\n";
                next;
            }
    
            my $finalName = "$narDiffHash.nar-bsdiff";

            if (-e "$patchesPath/$finalName") {
                print "    not copying, already exists\n";
            }

            else {
                system("cp '$tmpDir/DIFF' '$patchesPath/$finalName.tmp'") == 0
                    or die "cannot copy diff";
                rename("$patchesPath/$finalName.tmp", "$patchesPath/$finalName")
                    or die "cannot rename $patchesPath/$finalName.tmp";
            }
        
            # Add the patch to the manifest.
            addPatch $dstPatches, $p,
                { url => "$patchesURL/$finalName", hash => "$hashAlgo:$narDiffHash"
                , size => $narDiffSize, basePath => $closest, baseHash => "$hashAlgo:$baseHash"
                , narHash => "$hashAlgo:$narHash", patchType => "nar-bsdiff"
                };
        }
    }
}


# Propagate useful patches from $srcPatches to $dstPatches.  A patch
# is useful if it produces either paths in the $dstNarFiles or paths
# that can be used as the base for other useful patches.
sub propagatePatches {
    my ($srcPatches, $dstNarFiles, $dstPatches) = @_;

    print STDERR "propagating patches...\n";

    my $changed;
    do {
        # !!! we repeat this to reach the transitive closure; inefficient
        $changed = 0;

        print STDERR "loop\n";

        my %dstBasePaths;
        foreach my $q (keys %{$dstPatches}) {
            foreach my $patch (@{$$dstPatches{$q}}) {
                $dstBasePaths{$patch->{basePath}} = 1;
            }
        }

        foreach my $p (keys %{$srcPatches}) {
            my $patchList = $$srcPatches{$p};

            my $include = 0;

            # Is path $p included in the destination?  If so, include
            # patches that produce it.
            $include = 1 if defined $$dstNarFiles{$p};

            # Is path $p a path that serves as a base for paths in the
            # destination?  If so, include patches that produce it.
            # !!! check baseHash
            $include = 1 if defined $dstBasePaths{$p};

            if ($include) {
                foreach my $patch (@{$patchList}) {
                    $changed = 1 if addPatch $dstPatches, $p, $patch;
                }
            }
        
        }
    
    } while $changed;
}


# Add all new patches in $srcPatches to $dstPatches.
sub copyPatches {
    my ($srcPatches, $dstPatches) = @_;
    foreach my $p (keys %{$srcPatches}) {
        addPatch $dstPatches, $p, $_ foreach @{$$srcPatches{$p}};
    }
}


return 1;
