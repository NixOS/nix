package Nix::Store;

use strict;
use warnings;

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
    getBinDir getStoreDir
);

our $VERSION = '0.15';

sub backtick {
    open(RES, "-|", @_) or die;
    local $/;
    my $res = <RES> || "";
    close RES or die;
    return $res;
}

require XSLoader;
XSLoader::load('Nix::Store', $VERSION);

1;
__END__
