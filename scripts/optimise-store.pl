#! /usr/bin/perl -w

use strict;
use File::Basename;


my @paths = ("/nix/store");


print "hashing...\n";

my $hashList = "/tmp/nix-optimise-hash-list";

system("find @paths -type f -print0 | xargs -0 md5sum -- > $hashList") == 0
    or die "cannot hash store files";


print "sorting by hash...\n";

system("sort $hashList > $hashList.sorted") == 0
    or die "cannot sort list";


sub atomicLink {
    my $target = shift;
    my $new = shift;
    my $tmpNew = "${new}_optimise.$$";

    # Make the directory writable temporarily.
    my $dir = dirname $new;
    my @st = stat $dir or die;

    chmod ($st[2] | 0200, $dir) or die "cannot make `$dir' writable: $!";
    
    link $target, $tmpNew or die "cannot create hard link `$tmpNew': $!";

    rename $tmpNew, $new or die "cannot rename `$tmpNew' to `$new': $!";

    chmod ($st[2], $dir) or die "cannot restore permission on `$dir': $!";
    utime ($st[8], $st[9], $dir) or die "cannot restore timestamp on `$dir': $!";
}


print "hard-linking...\n";

open LIST, "<$hashList.sorted" or die;

my $prevFile;
my $prevHash;
my $prevInode;
my $prevExec;

my $totalSpace = 0;
my $savedSpace = 0;

while (<LIST>) {
    /^([0-9a-f]*)\s+(.*)$/ or die;
    my $curFile = $2;
    my $curHash = $1;

    my @st = stat $curFile or die;
    next if ($st[2] & 0222) != 0; # skip writable files

    my $fileSize = $st[7];
    $totalSpace += $fileSize;
    my $isExec = ($st[2] & 0111) == 0111;

    if (defined $prevHash && $curHash eq $prevHash
        && $prevExec == $isExec)
    {
        
        if ($st[1] != $prevInode) {
            print "$curFile = $prevFile\n";
            atomicLink $prevFile, $curFile;
            $savedSpace += $fileSize;
        }
        
    } else {
        $prevFile = $curFile;
        $prevHash = $curHash;
        $prevInode = $st[1];
        $prevExec = ($st[2] & 0111) == 0111;
    }
}

print "total space = $totalSpace\n";
print "saved space = $savedSpace\n";
my $savings = ($savedSpace / $totalSpace) * 100.0;
print "savings = $savings %\n";

close LIST;
