#! /usr/bin/perl -w -I /home/eelco/.nix-profile/lib/site_perl

use strict;
use XML::LibXML;
#use XML::Simple;

my $blacklistFN = shift @ARGV;
die unless defined $blacklistFN;
my $userEnv = shift @ARGV;
die unless defined $userEnv;


# Read the blacklist.
my $parser = XML::LibXML->new();
my $blacklist = $parser->parse_file($blacklistFN)->getDocumentElement;

#print $blacklist->toString() , "\n";


# Get all the elements of the user environment.
my $userEnvElems = `nix-store --query --references '$userEnv'`;
die "cannot query user environment elements" if $? != 0;
my @userEnvElems = split ' ', $userEnvElems;


my %storePathHashes;


# Function for evaluating conditions.
sub evalCondition {
    my $storePaths = shift;
    my $condition = shift;

    my $name = $condition->getName;
    
    if ($name eq "containsSource") {
        my $hash = $condition->attributes->getNamedItem("hash")->getValue;
        foreach my $path (keys %{$storePathHashes{$hash}}) {
            # !!! use a hash for $storePaths
            foreach my $path2 (@{$storePaths}) {
                return 1 if $path eq $path2;
            }
        }
        return 0;
    }

    elsif ($name eq "and") {
        my $result = 1;
        foreach my $node ($condition->getChildNodes) {
            if ($node->nodeType == XML_ELEMENT_NODE) {
                $result &= evalCondition($storePaths, $node);
            }
        }
        return $result;
    }

    elsif ($name eq "true") {
        return 1;
    }

    elsif ($name eq "false") {
        return 0;
    }

    else {
        die "unknown element `$name'";
    }
}


sub evalOr {
    my $storePaths = shift;
    my $nodes = shift;

    my $result = 0;
    foreach my $node (@{$nodes}) {
        if ($node->nodeType == XML_ELEMENT_NODE) {
            $result |= evalCondition($storePaths, $node);
        }
    }
    
    return $result;
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
    foreach my $item ($blacklist->getChildrenByTagName("item")) {
        my $itemId = $item->getAttributeNode("id")->getValue;
        print "  CHECKING FOR $itemId\n";

        my $condition = ($item->getChildrenByTagName("condition"))[0];
        die unless $condition;

        # Evaluate the condition.
        my @foo = $condition->getChildNodes();
        if (evalOr(\@requisites, \@foo)) {
            # Oops, condition triggered.
            my $reason = ($item->getChildrenByTagName("reason"))[0]->getChildNodes->to_literal;
            $reason =~ s/\s+/ /g;
            $reason =~ s/^\s+//g;

            print "    VULNERABLE TO `$itemId': $reason\n";
        }
    }
}

