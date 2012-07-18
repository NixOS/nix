package Nix::Store;

use strict;
use warnings;
use Nix::Config;

require Exporter;

our @ISA = qw(Exporter);

our %EXPORT_TAGS = ( 'all' => [ qw( ) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
    isValidPath queryReferences queryPathInfo queryDeriver queryPathHash
    queryPathFromHashPart
    topoSortPaths computeFSClosure followLinksToStorePath exportPaths
    hashPath hashFile hashString
    addToStore makeFixedOutputPath
    derivationFromPath
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

    sub hashFile {
        my ($algo, $base32, $path) = @_;
        my $res = backtick("$Nix::Config::binDir/nix-hash", "--flat", $path, "--type", $algo, $base32 ? "--base32" : ());
        chomp $res;
        return $res;
    }
    
    sub hashPath {
        my ($algo, $base32, $path) = @_;
        my $res = backtick("$Nix::Config::binDir/nix-hash", $path, "--type", $algo, $base32 ? "--base32" : ());
        chomp $res;
        return $res;
    }
    
    sub hashString {
        my ($algo, $base32, $s) = @_;
        my $fh = File::Temp->new();
        print $fh $s;
        my $res = backtick("$Nix::Config::binDir/nix-hash", $fh->filename, "--type", $algo, $base32 ? "--base32" : ());
        chomp $res;
        return $res;
    }
    
    sub addToStore {
        my ($srcPath, $recursive, $algo) = @_;
        die "not implemented" if $recursive || $algo ne "sha256";
        my $res = backtick("$Nix::Config::binDir/nix-store", "--add", $srcPath);
        chomp $res;
        return $res;
    }
    
    sub isValidPath {
        my ($path) = @_;
        my $res = backtick("$Nix::Config::binDir/nix-store", "--check-validity", "--print-invalid", $path);
        chomp $res;
        return $res ne $path;
    }
    
    sub queryPathHash {
        my ($path) = @_;
        my $res = backtick("$Nix::Config::binDir/nix-store", "--query", "--hash", $path);
        chomp $res;
        return $res;
    }
}

1;
__END__
