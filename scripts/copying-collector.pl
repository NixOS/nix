#! /usr/bin/perl -w

use strict;

my @paths = `nix-store -qR /home/eelco/.nix-profile/bin/firefox`;

my %copyMap;
my %rewriteMap;


my $counter = 0;

foreach my $path (@paths) {
    chomp $path;

    $path =~ /^(.*)\/([^-]+)-(.*)$/ or die "invalid store path `$path'";
    my $hash = $2;

#    my $newHash = "deadbeef" . (sprintf "%024d", $counter++);
    my $newHash = "deadbeef" . substr($hash, 0, 24);
    my $newPath = "/home/eelco/chroot/$1/$newHash-$3";

    die unless length $newHash == length $hash;

    $copyMap{$path} = $newPath;
    $rewriteMap{$hash} = $newHash;
}


my %rewriteMap2;


sub rewrite;
sub rewrite {
    my $src = shift;
    my $dst = shift;

    if (-l $dst) {

        my $target = readlink $dst or die;

        foreach my $srcHash (keys %rewriteMap2) {
            my $dstHash = $rewriteMap{$srcHash};
            print "  $srcHash -> $dstHash\n";
            $target =~ s/$srcHash/$dstHash/g;
        }

        unlink $dst or die;

        symlink $target, $dst;
        
    }

    elsif (-f $dst) {

        print "$dst\n";

        foreach my $srcHash (keys %rewriteMap2) {
            my $dstHash = $rewriteMap{$srcHash};
            print "  $srcHash -> $dstHash\n";

            my @stats = lstat $dst or die;
            
            system "sed s/$srcHash/$dstHash/g < '$dst' > '$dst.tmp'";
            die if $? != 0;
            rename "$dst.tmp", $dst or die;

            chmod $stats[2], $dst or die;
        }

    }

    elsif (-d $dst) {

        chmod 0755, $dst;
        
        opendir(DIR, "$dst") or die "cannot open `$dst': $!";
        my @files = readdir DIR;
        closedir DIR;
        
        foreach my $file (@files) {
            next if $file eq "." || $file eq "..";
            rewrite "$src/$file", "$dst/$file";
        }
    }
}


foreach my $src (keys %copyMap) {
    my $dst = $copyMap{$src};
    print "$src -> $dst\n";

    if (!-e $dst) {
        system "cp -prd $src $dst";
        die if $? != 0;

        my @refs = `nix-store -q --references $src`;

        %rewriteMap2 = ();
        foreach my $ref (@refs) {
            chomp $ref;

            $ref =~ /^(.*)\/([^-]+)-(.*)$/ or die "invalid store path `$ref'";
            my $hash = $2;

            $rewriteMap2{$hash} = $rewriteMap{$hash};
        }

        rewrite $src, $dst;
    }
}
