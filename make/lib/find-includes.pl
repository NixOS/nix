use strict;

my $root = $ENV{"main"};
my $out = $ENV{"out"};

open OUT, ">$out" or die "$!";
print OUT "[\n";

open IN, "<$root" or die "$!";
while (<IN>) {
    if (/^\#include\s+\"(.*)\"/) {
        print OUT "\"$1\"\n";
    }
    if (/^\#include\s+\<(.*)\>/) {
        print OUT "\"$1\"\n";
    }
}
close IN;

print OUT "]\n";
close OUT;
