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


sub getElemNodes {
    my $node = shift;
    my @elems = ();
    foreach my $node ($node->getChildNodes) {
        push @elems, $node if $node->nodeType == XML_ELEMENT_NODE;
    }
    return @elems;
}


my %referencesCache;
sub getReferences {
    my $path = shift;
    return $referencesCache{$path} if defined $referencesCache{$path};
    
    my $references = `nix-store --query --references '$path'`;
    die "cannot query references" if $? != 0;
    $referencesCache{$path} = [split ' ', $references];
    
    return $referencesCache{$path};
}


my %attrsCache;
sub getAttr {
    my $path = shift;
    my $name = shift;
    my $key = "$path/$name";
    return $referencesCache{$key} if defined $referencesCache{$key};

    my $value = `nix-store --query --binding '$name' '$path' 2> /dev/null`;
    $value = "" if $? != 0; # !!!
    chomp $value;
    $referencesCache{$key} = $value;

    return $value;
}


sub evalCondition;


sub traverse {
    my $done = shift;
    my $set = shift;
    my $path = shift;
    my $stopCondition = shift;

    return if defined $done->{$path};
    $done->{$path} = 1;
    $set->{$path} = 1;

#    print "  in $path\n";

    if (!evalCondition({$path => 1}, $stopCondition)) {
#        print "  STOPPING in $path\n";
        return;
    }

    # Get the requisites of the deriver.

    foreach my $reference (@{getReferences $path}) {
        traverse($done, $set, $reference, $stopCondition);
    }
}


sub evalSet {
    my $inSet = shift;
    my $expr = shift;
    my $name = $expr->getName;
    
    if ($name eq "traverse") {
        my $stopCondition = (getElemNodes $expr)[0];
        my $done = { };
        my $set = { };
        foreach my $path (keys %{$inSet}) {
            traverse($done, $set, $path, $stopCondition);
        }
        return $set;
    }

    else {
        die "unknown element `$name'";
    }
}


# Function for evaluating conditions.
sub evalCondition {
    my $storePaths = shift;
    my $condition = shift;
    my $elemName = $condition->getName;
    
    if ($elemName eq "containsSource") {
        my $hash = $condition->attributes->getNamedItem("hash")->getValue;
        foreach my $path (keys %{$storePathHashes{$hash}}) {
            return 1 if defined $storePaths->{$path};
        }
        return 0;
    }

    elsif ($elemName eq "hasName") { 
        my $nameRE = $condition->attributes->getNamedItem("name")->getValue;
        foreach my $path (keys %{$storePaths}) {
            return 1 if $path =~ /$nameRE/;
        }
        return 0;
    }
    
    elsif ($elemName eq "hasAttr") { 
        my $name = $condition->attributes->getNamedItem("name")->getValue;
        my $valueRE = $condition->attributes->getNamedItem("value")->getValue;
        foreach my $path (keys %{$storePaths}) {
            if ($path =~ /\.drv$/) {
                my $value = getAttr($path, $name);
#                print "    $path $name $value\n";
                return 1 if $value =~ /$valueRE/;
            }
        }
        return 0;
    }
    
    elsif ($elemName eq "and") {
        my $result = 1;
        foreach my $node (getElemNodes $condition) {
            $result &= evalCondition($storePaths, $node);
        }
        return $result;
    }

    elsif ($elemName eq "not") {
        return !evalCondition($storePaths, (getElemNodes $condition)[0]);
    }
    
    elsif ($elemName eq "within") {
        my @elems = getElemNodes $condition;
        my $set = evalSet($storePaths, $elems[0]);
        return evalCondition($set, $elems[1]);
    }

    elsif ($elemName eq "true") {
        return 1;
    }

    elsif ($elemName eq "false") {
        return 0;
    }

    else {
        die "unknown element `$elemName'";
    }
}


sub evalOr {
    my $storePaths = shift;
    my $nodes = shift;

    my $result = 0;
    foreach my $node (@{$nodes}) {
        $result |= evalCondition($storePaths, $node);
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
#    my $requisites = `nix-store --query --requisites --include-outputs '$deriver'`;
#    die "cannot query requisites" if $? != 0;
#    my @requisites = split ' ', $requisites;


    # Get the hashes of the requisites.
#    my $hashes = `nix-store --query --hash @requisites`;
#    die "cannot query hashes" if $? != 0;
#    my @hashes = split ' ', $hashes;
#    for (my $i = 0; $i < scalar @requisites; $i++) {
#        die unless $i < scalar @hashes;
#        my $hash = $hashes[$i];
#        $storePathHashes{$hash} = {} unless defined $storePathHashes{$hash};
#        my $r = $storePathHashes{$hash}; # !!! fix
#        $$r{$requisites[$i]} = 1;
#    }


    # Evaluate each blacklist item.
    foreach my $item ($blacklist->getChildrenByTagName("item")) {
        my $itemId = $item->getAttributeNode("id")->getValue;
        print "  CHECKING FOR $itemId\n";

        my $condition = ($item->getChildrenByTagName("condition"))[0];
        die unless $condition;

        # Evaluate the condition.
        my @elems = getElemNodes $condition;
        if (evalOr({$deriver => 1}, \@elems)) {
            # Oops, condition triggered.
            my $reason = ($item->getChildrenByTagName("reason"))[0]->getChildNodes->to_literal;
            $reason =~ s/\s+/ /g;
            $reason =~ s/^\s+//g;

            print "    VULNERABLE TO `$itemId': $reason\n";
        }
    }
}

