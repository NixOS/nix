#! /usr/bin/perl -w

use strict;
use XML::Simple;

my $blacklistFN = shift @ARGV;
die unless defined $blacklistFN;
my $userEnv = shift @ARGV;
die unless defined $userEnv;


# Read the blacklist.
my $blacklist = XMLin($blacklistFN,
    forcearray => [qw()],
    keyattr => ['id'],
    suppressempty => '');


# Get all the elements of the user environment.
my $userEnvElems = `nix-store --query --references '$userEnv'`;
die "cannot query user environment elements" if $? != 0;
my @userEnvElems = split ' ', $userEnvElems;


my %storePathHashes;


# Function for evaluating conditions.
sub evalCondition {
    my $storePaths = shift;
    my $condition = shift;

    if (defined $condition->{'containsSource'}) {
        my $c = $condition->{'containsSource'};
        my $hash = $c->{'hash'};

        foreach my $path (keys %{$storePathHashes{$hash}}) {
            # !!! use a hash for $storePaths
            foreach my $path2 (@{$storePaths}) {
                return 1 if $path eq $path2;
            }
        }
        return 0;
    }
    
    return 0;
}


# Iterate over all elements, check them.
foreach my $userEnvElem (@userEnvElems) {

    # Get the deriver of this path.
    my $deriver = `nix-store --query --deriver '$userEnvElem'`;
    die "cannot query deriver" if $? != 0;
    chomp $deriver;

    if ($deriver eq "unknown-deriver") {
#        print "  deriver unknown, cannot check sources\n";
        next;
    }

    print "CHECKING $userEnvElem\n";


    # Get the requisites of the deriver.
    my $requisites = `nix-store --query --requisites --include-outputs '$deriver'`;
    die "cannot query requisites" if $? != 0;
    my @requisites = split ' ', $requisites;


    # Get the hashes of the requisites.
    my $hashes = `nix-store --query --hash @requisites`;
    die "cannot query hashes" if $? != 0;
    my @hashes = split ' ', $hashes;
    for (my $i = 0; $i < scalar @requisites; $i++) {
        die unless $i < scalar @hashes;
        my $hash = $hashes[$i];
        $storePathHashes{$hash} = {} unless defined $storePathHashes{$hash};
        my $r = $storePathHashes{$hash}; # !!! fix
        $$r{$requisites[$i]} = 1;
    }


    # Evaluate each blacklist item.
    foreach my $itemId (sort (keys %{$blacklist->{'item'}})) {
#        print "  CHECKING FOR $itemId\n";

        my $item = $blacklist->{'item'}->{$itemId};
        die unless defined $item;

        my $condition = $item->{'condition'};
        die unless defined $condition;

        # Evaluate the condition.
        if (evalCondition(\@requisites, $condition)) {

            # Oops, condition triggered.
            my $reason = $item->{'reason'};
            $reason =~ s/\s+/ /g;
            $reason =~ s/^\s+//g;

            print "    VULNERABLE TO `$itemId': $reason\n";
        }
    }
}

