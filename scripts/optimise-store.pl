#! /usr/bin/perl -w

use strict;

#{ my $ofh = select STDOUT;
#  $| = 1;
#  select $ofh;
#}

#my @paths = ("/nix/store/d49mc94xwwd7wf1xzfh4ch4cypn0ajjr-glibc-2.3.6", "/nix/store/1mgfgy3ga4m9z60747s0yzxl0g6w5kxz-glibc-2.3.6");
my @paths = ("/nix/store");

my $hashList = "/tmp/nix-optimise-hash-list";

system("find @paths -type f -print0 | xargs -0 md5sum -- > $hashList") == 0
    or die "cannot hash store files";

system("sort $hashList > $hashList.sorted") == 0
    or die "cannot sort list";

open LIST, "<$hashList.sorted" or die;

my $prevFile;
my $prevHash;

my $totalSpace = 0;
my $savedSpace = 0;

my $files = 0;

while (<LIST>) {
    /^([0-9a-f]*)\s+(.*)$/ or die;
    my $curFile = $2;
    my $curHash = $1;

    my $fileSize = (stat $curFile)[7];
    $totalSpace += $fileSize;

    if (defined $prevHash && $curHash eq $prevHash) {
        
        print "$curFile = $prevFile\n";

        $savedSpace += $fileSize;
        
    } else {
        $prevFile = $curFile;
        $prevHash = $curHash;
    }

#    print "." if ($files++ % 100 == 0);
}

#print "\n";

print "total space = $totalSpace\n";
print "saved space = $savedSpace\n";
my $savings = ($savedSpace / $totalSpace) * 100.0;
print "savings = $savings %\n";


close LIST;
