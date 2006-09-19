#! /usr/bin/perl -w

use strict;

{ my $ofh = select STDOUT;
  $| = 1;
  select $ofh;
}

my @paths = ("/nix/store");

my $tmpfile = "/tmp/nix-optimise-hash-list";
#my $tmpfile = "/data/nix-optimise-hash-list";

system("find @paths -type f -print0 | xargs -0 md5sum -- > $tmpfile") == 0
    or die "cannot hash store files";

system("sort $tmpfile > $tmpfile.sorted") == 0
    or die "cannot sort list";

open LIST, "<$tmpfile.sorted" or die;

my $prevFile;
my $prevHash;

my $totalSpace = 0;
my $savedSpace = 0;

my $files = 0;

while (<LIST>) {
#    print "D";
    /^([0-9a-f]*)\s+(.*)$/ or die;
    my $curFile = $2;
    my $curHash = $1;

#    print "A";
    my $fileSize = (stat $curFile)[7];
#    print "B";
#    my $fileSize = 1;
    $totalSpace += $fileSize;

    if (defined $prevHash && $curHash eq $prevHash) {
        
#        print "$curFile = $prevFile\n";

        $savedSpace += $fileSize;
        
    } else {
        $prevFile = $curFile;
        $prevHash = $curHash;
    }

    print "." if ($files++ % 100 == 0);
    #print ".";

#    print "C";
}

print "\n";

print "total space = $totalSpace\n";
print "saved space = $savedSpace\n";
my $savings = ($savedSpace / $totalSpace) * 100.0;
print "savings = $savings %\n";


close LIST;
