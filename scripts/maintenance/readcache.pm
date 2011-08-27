package readcache;
use strict;

# Read the archive directories.
our %archives;

sub readDir {
    my $dir = shift;
    opendir(DIR, "$dir") or die "cannot open `$dir': $!";
    my @as = readdir DIR;
    foreach my $archive (@as) {
        next unless $archive =~ /^sha256_/ || $archive =~ /\.nar-bsdiff$/ || $archive =~ /\.nar\.bz2$/;
        $archives{$archive} = "$dir/$archive";
    }
    closedir DIR;
}

readDir "/data/releases/nars";
readDir "/data/releases/patches";

print STDERR scalar (keys %archives), "\n";
