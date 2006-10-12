package readcache;
use strict;

# Read the archive directories.
our %archives;

sub readDir {
    my $dir = shift;
    opendir(DIR, "$dir") or die "cannot open `$dir': $!";
    my @as = readdir DIR;
    foreach my $archive (@as) {
        $archives{$archive} = "$dir/$archive";
    }
    closedir DIR;
}

readDir "/data/webserver/dist/nix-cache";
readDir "/data/webserver/dist/test-cache";
readDir "/data/webserver/dist/patches";

print STDERR scalar (keys %archives), "\n";
