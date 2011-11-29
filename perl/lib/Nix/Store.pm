package Nix::Store;

use 5.010001;
use strict;
use warnings;

require Exporter;

our @ISA = qw(Exporter);

our %EXPORT_TAGS = ( 'all' => [ qw( ) ] );

our @EXPORT_OK = ( @{ $EXPORT_TAGS{'all'} } );

our @EXPORT = qw(
    isValidPath queryReferences queryPathInfo queryDeriver queryPathHash
    topoSortPaths computeFSClosure followLinksToStorePath exportPaths
    hashPath
);

our $VERSION = '0.15';

require XSLoader;
XSLoader::load('Nix::Store', $VERSION);

1;
__END__
