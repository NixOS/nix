package Nix::Manifest;

use strict;
use DBI;
use Cwd;
use File::stat;
use File::Path;
use Fcntl ':flock';
use Nix::Config;

our @ISA = qw(Exporter);
our @EXPORT = qw(readManifest writeManifest updateManifestDB addPatch deleteOldManifests);


sub addNAR {
    my ($narFiles, $storePath, $info) = @_;

    $$narFiles{$storePath} = []
        unless defined $$narFiles{$storePath};

    my $narFileList = $$narFiles{$storePath};

    my $found = 0;
    foreach my $narFile (@{$narFileList}) {
        $found = 1 if $narFile->{url} eq $info->{url};
    }

    push @{$narFileList}, $info if !$found;
}


sub addPatch {
    my ($patches, $storePath, $patch) = @_;

    $$patches{$storePath} = []
        unless defined $$patches{$storePath};

    my $patchList = $$patches{$storePath};

    my $found = 0;
    foreach my $patch2 (@{$patchList}) {
        $found = 1 if
            $patch2->{url} eq $patch->{url} &&
            $patch2->{basePath} eq $patch->{basePath};
    }

    push @{$patchList}, $patch if !$found;

    return !$found;
}


sub readManifest_ {
    my ($manifest, $addNAR, $addPatch) = @_;

    # Decompress the manifest if necessary.
    if ($manifest =~ /\.bz2$/) {
        open MANIFEST, "$Nix::Config::bzip2 -d < $manifest |"
            or die "cannot decompress `$manifest': $!";
    } else {
        open MANIFEST, "<$manifest"
            or die "cannot open `$manifest': $!";
    }

    my $inside = 0;
    my $type;

    my $manifestVersion = 2;

    my ($storePath, $url, $hash, $size, $basePath, $baseHash, $patchType);
    my ($narHash, $narSize, $references, $deriver, $copyFrom, $system);

    while (<MANIFEST>) {
        chomp;
        s/\#.*$//g;
        next if (/^$/);

        if (!$inside) {

            if (/^\s*(\w*)\s*\{$/) {
                $type = $1;
                $type = "narfile" if $type eq "";
                $inside = 1;
                undef $storePath;
                undef $url;
                undef $hash;
                undef $size;
                undef $narHash;
                undef $narSize;
                undef $basePath;
                undef $baseHash;
                undef $patchType;
                undef $system;
                $references = "";
                $deriver = "";
            }

        } else {

            if (/^\}$/) {
                $inside = 0;

                if ($type eq "narfile") {
                    &$addNAR($storePath,
                        { url => $url, hash => $hash, size => $size
                        , narHash => $narHash, narSize => $narSize
                        , references => $references
                        , deriver => $deriver
                        , system => $system
                        });
                }

                elsif ($type eq "patch") {
                    &$addPatch($storePath,
                        { url => $url, hash => $hash, size => $size
                        , basePath => $basePath, baseHash => $baseHash
                        , narHash => $narHash, narSize => $narSize
                        , patchType => $patchType
                        });
                }

            }

            elsif (/^\s*StorePath:\s*(\/\S+)\s*$/) { $storePath = $1; }
            elsif (/^\s*CopyFrom:\s*(\/\S+)\s*$/) { $copyFrom = $1; }
            elsif (/^\s*Hash:\s*(\S+)\s*$/) { $hash = $1; }
            elsif (/^\s*URL:\s*(\S+)\s*$/) { $url = $1; }
            elsif (/^\s*Size:\s*(\d+)\s*$/) { $size = $1; }
            elsif (/^\s*BasePath:\s*(\/\S+)\s*$/) { $basePath = $1; }
            elsif (/^\s*BaseHash:\s*(\S+)\s*$/) { $baseHash = $1; }
            elsif (/^\s*Type:\s*(\S+)\s*$/) { $patchType = $1; }
            elsif (/^\s*NarHash:\s*(\S+)\s*$/) { $narHash = $1; }
            elsif (/^\s*NarSize:\s*(\d+)\s*$/) { $narSize = $1; }
            elsif (/^\s*References:\s*(.*)\s*$/) { $references = $1; }
            elsif (/^\s*Deriver:\s*(\S+)\s*$/) { $deriver = $1; }
            elsif (/^\s*ManifestVersion:\s*(\d+)\s*$/) { $manifestVersion = $1; }
            elsif (/^\s*System:\s*(\S+)\s*$/) { $system = $1; }

            # Compatibility;
            elsif (/^\s*NarURL:\s*(\S+)\s*$/) { $url = $1; }
            elsif (/^\s*MD5:\s*(\S+)\s*$/) { $hash = "md5:$1"; }

        }
    }

    close MANIFEST;

    return $manifestVersion;
}


sub readManifest {
    my ($manifest, $narFiles, $patches) = @_;
    readManifest_($manifest,
        sub { addNAR($narFiles, @_); },
        sub { addPatch($patches, @_); } );
}


sub writeManifest {
    my ($manifest, $narFiles, $patches, $noCompress) = @_;

    open MANIFEST, ">$manifest.tmp"; # !!! check exclusive

    print MANIFEST "version {\n";
    print MANIFEST "  ManifestVersion: 3\n";
    print MANIFEST "}\n";

    foreach my $storePath (sort (keys %{$narFiles})) {
        my $narFileList = $$narFiles{$storePath};
        foreach my $narFile (@{$narFileList}) {
            print MANIFEST "{\n";
            print MANIFEST "  StorePath: $storePath\n";
            print MANIFEST "  NarURL: $narFile->{url}\n";
            print MANIFEST "  Hash: $narFile->{hash}\n" if defined $narFile->{hash};
            print MANIFEST "  Size: $narFile->{size}\n" if defined $narFile->{size};
            print MANIFEST "  NarHash: $narFile->{narHash}\n";
            print MANIFEST "  NarSize: $narFile->{narSize}\n" if $narFile->{narSize};
            print MANIFEST "  References: $narFile->{references}\n"
                if defined $narFile->{references} && $narFile->{references} ne "";
            print MANIFEST "  Deriver: $narFile->{deriver}\n"
                if defined $narFile->{deriver} && $narFile->{deriver} ne "";
            print MANIFEST "  System: $narFile->{system}\n" if defined $narFile->{system};
            print MANIFEST "}\n";
        }
    }

    foreach my $storePath (sort (keys %{$patches})) {
        my $patchList = $$patches{$storePath};
        foreach my $patch (@{$patchList}) {
            print MANIFEST "patch {\n";
            print MANIFEST "  StorePath: $storePath\n";
            print MANIFEST "  NarURL: $patch->{url}\n";
            print MANIFEST "  Hash: $patch->{hash}\n";
            print MANIFEST "  Size: $patch->{size}\n";
            print MANIFEST "  NarHash: $patch->{narHash}\n";
            print MANIFEST "  NarSize: $patch->{narSize}\n" if $patch->{narSize};
            print MANIFEST "  BasePath: $patch->{basePath}\n";
            print MANIFEST "  BaseHash: $patch->{baseHash}\n";
            print MANIFEST "  Type: $patch->{patchType}\n";
            print MANIFEST "}\n";
        }
    }


    close MANIFEST;

    rename("$manifest.tmp", $manifest)
        or die "cannot rename $manifest.tmp: $!";


    # Create a bzipped manifest.
    unless (defined $noCompress) {
        system("$Nix::Config::bzip2 < $manifest > $manifest.bz2.tmp") == 0
            or die "cannot compress manifest";

        rename("$manifest.bz2.tmp", "$manifest.bz2")
            or die "cannot rename $manifest.bz2.tmp: $!";
    }
}


sub updateManifestDB {
    my $manifestDir = $Nix::Config::manifestDir;

    mkpath($manifestDir);

    my $dbPath = "$manifestDir/cache.sqlite";

    # Open/create the database.
    our $dbh = DBI->connect("dbi:SQLite:dbname=$dbPath", "", "")
        or die "cannot open database `$dbPath'";
    $dbh->{RaiseError} = 1;
    $dbh->{PrintError} = 0;

    $dbh->do("pragma foreign_keys = on");
    $dbh->do("pragma synchronous = off"); # we can always reproduce the cache
    $dbh->do("pragma journal_mode = truncate");

    # Initialise the database schema, if necessary.
    $dbh->do(<<EOF);
        create table if not exists Manifests (
            id        integer primary key autoincrement not null,
            path      text unique not null,
            timestamp integer not null
        );
EOF

    $dbh->do(<<EOF);
        create table if not exists NARs (
            id               integer primary key autoincrement not null,
            manifest         integer not null,
            storePath        text not null,
            url              text not null,
            hash             text,
            size             integer,
            narHash          text,
            narSize          integer,
            refs             text,
            deriver          text,
            system           text,
            foreign key (manifest) references Manifests(id) on delete cascade
        );
EOF

    $dbh->do("create index if not exists NARs_storePath on NARs(storePath)");

    $dbh->do(<<EOF);
        create table if not exists Patches (
            id               integer primary key autoincrement not null,
            manifest         integer not null,
            storePath        text not null,
            basePath         text not null,
            baseHash         text not null,
            url              text not null,
            hash             text,
            size             integer,
            narHash          text,
            narSize          integer,
            patchType        text not null,
            foreign key (manifest) references Manifests(id) on delete cascade
        );
EOF

    $dbh->do("create index if not exists Patches_storePath on Patches(storePath)");

    # Acquire an exclusive lock to ensure that only one process
    # updates the DB at the same time.  This isn't really necessary,
    # but it prevents work duplication and lock contention in SQLite.
    my $lockFile = "$manifestDir/cache.lock";
    open MAINLOCK, ">>$lockFile" or die "unable to acquire lock ‘$lockFile’: $!\n";
    flock(MAINLOCK, LOCK_EX) or die;

    our $insertNAR = $dbh->prepare(
        "insert into NARs(manifest, storePath, url, hash, size, narHash, " .
        "narSize, refs, deriver, system) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)") or die;

    our $insertPatch = $dbh->prepare(
        "insert into Patches(manifest, storePath, basePath, baseHash, url, hash, " .
        "size, narHash, narSize, patchType) values (?, ?, ?, ?, ?, ?, ?, ?, ?, ?)");

    $dbh->begin_work;

    # Read each manifest in $manifestDir and add it to the database,
    # unless we've already done so on a previous run.
    my %seen;

    for my $manifestLink (glob "$manifestDir/*.nixmanifest") {
        my $manifest = Cwd::abs_path($manifestLink);
        next unless -f $manifest;
        my $timestamp = lstat($manifest)->mtime;
        $seen{$manifest} = 1;

        next if scalar @{$dbh->selectcol_arrayref(
            "select 1 from Manifests where path = ? and timestamp = ?",
            {}, $manifest, $timestamp)} == 1;

        print STDERR "caching $manifest...\n";

        $dbh->do("delete from Manifests where path = ?", {}, $manifest);

        $dbh->do("insert into Manifests(path, timestamp) values (?, ?)",
                 {}, $manifest, $timestamp);

        our $id = $dbh->last_insert_id("", "", "", "");

        sub addNARToDB {
            my ($storePath, $narFile) = @_;
            $insertNAR->execute(
                $id, $storePath, $narFile->{url}, $narFile->{hash}, $narFile->{size},
                $narFile->{narHash}, $narFile->{narSize}, $narFile->{references},
                $narFile->{deriver}, $narFile->{system});
        };

        sub addPatchToDB {
            my ($storePath, $patch) = @_;
            $insertPatch->execute(
                $id, $storePath, $patch->{basePath}, $patch->{baseHash}, $patch->{url},
                $patch->{hash}, $patch->{size}, $patch->{narHash}, $patch->{narSize},
                $patch->{patchType});
        };

        my $version = readManifest_($manifest, \&addNARToDB, \&addPatchToDB);

        if ($version < 3) {
            die "you have an old-style or corrupt manifest `$manifestLink'; please delete it\n";
        }
        if ($version >= 10) {
            die "manifest `$manifestLink' is too new; please delete it or upgrade Nix\n";
        }
    }

    # Removed cached information for removed manifests from the DB.
    foreach my $manifest (@{$dbh->selectcol_arrayref("select path from Manifests")}) {
        next if defined $seen{$manifest};
        $dbh->do("delete from Manifests where path = ?", {}, $manifest);
    }

    $dbh->commit;

    close MAINLOCK;

    return $dbh;
}



# Delete all old manifests downloaded from a given URL.
sub deleteOldManifests {
    my ($url, $curUrlFile) = @_;
    for my $urlFile (glob "$Nix::Config::manifestDir/*.url") {
        next if defined $curUrlFile && $urlFile eq $curUrlFile;
        open URL, "<$urlFile" or die;
        my $url2 = <URL>;
        chomp $url2;
        close URL;
        next unless $url eq $url2;
        my $base = $urlFile; $base =~ s/.url$//;
        unlink "${base}.url";
        unlink "${base}.nixmanifest";
    }
}


return 1;
