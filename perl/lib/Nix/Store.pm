package Nix::Store;

use strict;
use warnings;
use Nix::Config;

require Exporter;

our @ISA = qw(Exporter);

our %EXPORT_TAGS = ( 'all' => [ qw( ) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
    setVerbosity
    isValidPath queryReferences queryPathInfo queryDeriver queryPathHash
    queryPathFromHashPart
    topoSortPaths computeFSClosure followLinksToStorePath exportPaths importPaths
    hashPath hashFile hashString convertHash
    signString checkSignature
    addToStore makeFixedOutputPath
    derivationFromPath
    addTempRoot
);

our $VERSION = '0.15';

sub backtick {
    open(RES, "-|", @_) or die;
    local $/;
    my $res = <RES> || "";
    close RES or die;
    return $res;
}

if ($Nix::Config::useBindings) {
    require XSLoader;
    XSLoader::load('Nix::Store', $VERSION);
} else {

    # Provide slow fallbacks of some functions on platforms that don't
    # support the Perl bindings.

    use File::Temp;
    use Fcntl qw/F_SETFD/;

    *hashFile = sub {
        my ($algo, $base32, $path) = @_;
        my $res = backtick("$Nix::Config::binDir/nix-hash", "--flat", $path, "--type", $algo, $base32 ? "--base32" : ());
        chomp $res;
        return $res;
    };

    *hashPath = sub {
        my ($algo, $base32, $path) = @_;
        my $res = backtick("$Nix::Config::binDir/nix-hash", $path, "--type", $algo, $base32 ? "--base32" : ());
        chomp $res;
        return $res;
    };

    *hashString = sub {
        my ($algo, $base32, $s) = @_;
        my $fh = File::Temp->new();
        print $fh $s;
        my $res = backtick("$Nix::Config::binDir/nix-hash", $fh->filename, "--type", $algo, $base32 ? "--base32" : ());
        chomp $res;
        return $res;
    };

    *addToStore = sub {
        my ($srcPath, $recursive, $algo) = @_;
        die "not implemented" if $recursive || $algo ne "sha256";
        my $res = backtick("$Nix::Config::binDir/nix-store", "--add", $srcPath);
        chomp $res;
        return $res;
    };

    *isValidPath = sub {
        my ($path) = @_;
        my $res = backtick("$Nix::Config::binDir/nix-store", "--check-validity", "--print-invalid", $path);
        chomp $res;
        return $res ne $path;
    };

    *queryPathHash = sub {
        my ($path) = @_;
        my $res = backtick("$Nix::Config::binDir/nix-store", "--query", "--hash", $path);
        chomp $res;
        return $res;
    };
}

1;
__END__
