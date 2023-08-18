package Nix::Manifest;

use utf8;
use strict;
use DBI;
use DBD::SQLite;
use Cwd;
use File::stat;
use File::Path;
use Fcntl ':flock';
use MIME::Base64;
use Nix::Config;
use Nix::Store;

our @ISA = qw(Exporter);
our @EXPORT = qw(readManifest writeManifest addPatch parseNARInfo fingerprintPath);


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
            or die "cannot decompress '$manifest': $!";
    } else {
        open MANIFEST, "<$manifest"
            or die "cannot open '$manifest': $!";
    }

    my $inside = 0;
    my $type;

    my $manifestVersion = 2;

    my ($storePath, $url, $hash, $size, $basePath, $baseHash, $patchType);
    my ($narHash, $narSize, $references, $deriver, $copyFrom, $system, $compressionType);

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
                $compressionType = "bzip2";
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
                        , compressionType => $compressionType
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
            elsif (/^\s*Compression:\s*(\S+)\s*$/) { $compressionType = $1; }
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
            print MANIFEST "  Compression: $narFile->{compressionType}\n";
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


# Return a fingerprint of a store path to be used in binary cache
# signatures. It contains the store path, the base-32 SHA-256 hash of
# the contents of the path, and the references.
sub fingerprintPath {
    my ($storePath, $narHash, $narSize, $references) = @_;
    die if substr($storePath, 0, length($Nix::Config::storeDir)) ne $Nix::Config::storeDir;
    die if substr($narHash, 0, 7) ne "sha256:";
    # Convert hash from base-16 to base-32, if necessary.
    $narHash = "sha256:" . convertHash("sha256", substr($narHash, 7), 1)
        if length($narHash) == 71;
    die if length($narHash) != 59;
    foreach my $ref (@{$references}) {
        die if substr($ref, 0, length($Nix::Config::storeDir)) ne $Nix::Config::storeDir;
    }
    return "1;" . $storePath . ";" . $narHash . ";" . $narSize . ";" . join(",", @{$references});
}


# Parse a NAR info file.
sub parseNARInfo {
    my ($storePath, $content, $requireValidSig, $location) = @_;

    my ($storePath2, $url, $fileHash, $fileSize, $narHash, $narSize, $deriver, $system, $sig);
    my $compression = "bzip2";
    my @refs;

    foreach my $line (split "\n", $content) {
        return undef unless $line =~ /^(.*): (.*)$/;
        if ($1 eq "StorePath") { $storePath2 = $2; }
        elsif ($1 eq "URL") { $url = $2; }
        elsif ($1 eq "Compression") { $compression = $2; }
        elsif ($1 eq "FileHash") { $fileHash = $2; }
        elsif ($1 eq "FileSize") { $fileSize = int($2); }
        elsif ($1 eq "NarHash") { $narHash = $2; }
        elsif ($1 eq "NarSize") { $narSize = int($2); }
        elsif ($1 eq "References") { @refs = split / /, $2; }
        elsif ($1 eq "Deriver") { $deriver = $2; }
        elsif ($1 eq "System") { $system = $2; }
        elsif ($1 eq "Sig") { $sig = $2; }
    }

    return undef if $storePath ne $storePath2 || !defined $url || !defined $narHash;

    my $res =
        { url => $url
        , compression => $compression
        , fileHash => $fileHash
        , fileSize => $fileSize
        , narHash => $narHash
        , narSize => $narSize
        , refs => [ @refs ]
        , deriver => $deriver
        , system => $system
        };

    if ($requireValidSig) {
        # FIXME: might be useful to support multiple signatures per .narinfo.

        if (!defined $sig) {
            warn "NAR info file '$location' lacks a signature; ignoring\n";
            return undef;
        }
        my ($keyName, $sig64) = split ":", $sig;
        return undef unless defined $keyName && defined $sig64;

        my $publicKey = $Nix::Config::binaryCachePublicKeys{$keyName};
        if (!defined $publicKey) {
            warn "NAR info file '$location' is signed by unknown key '$keyName'; ignoring\n";
            return undef;
        }

        my $fingerprint;
        eval {
            $fingerprint = fingerprintPath(
                $storePath, $narHash, $narSize,
                [ map { "$Nix::Config::storeDir/$_" } @refs ]);
        };
        if ($@) {
            warn "cannot compute fingerprint of '$location'; ignoring\n";
            return undef;
        }

        if (!checkSignature($publicKey, decode_base64($sig64), $fingerprint)) {
            warn "NAR info file '$location' has an incorrect signature; ignoring\n";
            return undef;
        }

        $res->{signedBy} = $keyName;
    }

    return $res;
}


return 1;
