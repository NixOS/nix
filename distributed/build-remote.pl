#! /usr/bin/perl -w

use strict;

my $amWilling = shift @ARGV;
my $localSystem = shift @ARGV;
my $neededSystem = shift @ARGV;
my $storeExpr = shift @ARGV;

sub sendReply {
    my $reply = shift;
    open OUT, ">&3" or die;
    print OUT "$reply\n";
    close OUT;
}

# Decline if the local system can do the build.
if ($amWilling && ($localSystem eq $neededSystem)) {
    sendReply "decline";
    exit 0;
}

# Otherwise find a willing remote machine.
my %machines;
my %systemTypes;
my %sshKeys;
my %maxJobs;
my %curJobs;

# Read the list of machines.
open CONF, "< /home/eelco/nix/distributed/remote-systems.conf" or die;

while (<CONF>) {
    chomp;
    next if /^\s*$/;
    /^\s*(\S+)\s+(\S+)\s+(\S+)\s+(\d+)\s*$/ or die;
    $machines{$1} = "";
    $systemTypes{$1} = $2;
    $sshKeys{$1} = $3;
    $maxJobs{$1} = $4;
}

close CONF;

# Read the current load status.
open LOAD, "< /home/eelco/nix/distributed/current-load" or die;
while (<LOAD>) {
    chomp;
    next if /^\s*$/;
    /^\s*(\S+)\s+(\d+)\s*$/ or die;
    $curJobs{$1} = $2;
}
close LOAD;

foreach my $cur (keys %machines) {
    $curJobs{$cur} = 0 unless defined $curJobs{$cur};
}

# Find a suitable system.
my $rightType = 0;
my $machine;
foreach my $cur (keys %machines) {
    if ($neededSystem eq $systemTypes{$cur}) {
        $rightType = 1;
        if ($curJobs{$cur} < $maxJobs{$cur})
        {
            $machine = $cur;
            last;
        }
    }
}

if (!defined $machine) {
    if ($rightType) {
        sendReply "postpone";
        exit 0;
    } else {
        sendReply "decline";
        exit 0;
    }
}

sendReply "accept";
open IN, "<&4" or die;
my $x = <IN>;
chomp $x;
print "got $x\n";  
close IN;

print "BUILDING REMOTE: $storeExpr on $machine\n";

$curJobs{$machine} = $curJobs{$machine} + 1;

sub writeLoad {
    open LOAD, "> /home/eelco/nix/distributed/current-load" or die;
    foreach my $cur (keys %machines) {
        print LOAD "$cur $curJobs{$cur}\n";
    }
    close LOAD;
}

writeLoad


my $ssh = "ssh -i $sshKeys{$machine} -x";

my $inputs = `cat inputs`;
$inputs =~ s/\n/ /g;

my $outputs = `cat outputs`;
$outputs =~ s/\n/ /g;

my $successors = `cat successors`;
$successors =~ s/\n/ /g;

system "rsync -a -e '$ssh' $storeExpr $inputs $machine:/nix/store";
die "cannot rsync inputs to $machine" if ($? != 0);

system "$ssh $machine /nix/bin/nix-store --validpath $storeExpr $inputs";
die "cannot set valid paths on $machine" if ($? != 0);

system "$ssh $machine /nix/bin/nix-store --successor $successors";
die "cannot set successors on $machine" if ($? != 0);

print "BUILDING...\n";

system "$ssh $machine /nix/bin/nix-store -qnfvvvv $storeExpr";
die "remote build on $machine failed" if ($? != 0);

print "REMOTE BUILD DONE\n";

foreach my $output (split '\n', $outputs) {
    system "rsync -a -e '$ssh' $machine:$output /nix/store";
    die "cannot rsync outputs from $machine" if ($? != 0);
}

$curJobs{$machine} = $curJobs{$machine} - 1;

writeLoad
