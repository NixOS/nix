#! /usr/bin/perl -w

# This program generates C/C++ code for efficiently manipulating
# ATerms.  It generates functions to build and match ATerms according
# to a set of constructor definitions defined in a file read from
# standard input.  A constructor is defined by a line with the
# following format:
#
#   SYM | ARGS | TYPE | FUN?
#
# where SYM is the name of the constructor, ARGS is a
# whitespace-separated list of argument types, TYPE is the type of the
# resulting ATerm (which should be `ATerm' or a type synonym for
# `ATerm'), and the optional FUN is used to construct the names of the
# build and match functions (it defaults to SYM; overriding it is
# useful if there are overloaded constructors, e.g., with different
# arities).  Note that SYM may be empty.
#
# A line of the form
#
#   VAR = EXPR
#
# causes a ATerm variable to be generated that is initialised to the
# value EXPR.
#
# Finally, a line of the form
#
#   init NAME
#
# causes the initialisation function to be called `NAME'.  This
# function must be called before any of the build/match functions or
# the generated variables are used.

die if scalar @ARGV != 2;

my $syms = "";
my $init = "";
my $initFun = "init";

open HEADER, ">$ARGV[0]";
open IMPL, ">$ARGV[1]";

while (<STDIN>) {
    next if (/^\s*$/);
    
    if (/^\s*(\w*)\s*\|([^\|]*)\|\s*(\w+)\s*\|\s*(\w+)?/) {
        my $const = $1;
        my @types = split ' ', $2;
        my $result = $3;
        my $funname = $4;
        $funname = $const unless defined $funname;

        my $formals = "";
        my $formals2 = "";
        my $args = "";
        my $unpack = "";
        my $n = 1;
        foreach my $type (@types) {
            my $realType = $type;
            $args .= ", ";
            if ($type eq "string") {
#                $args .= "(ATerm) ATmakeAppl0(ATmakeAFun((char *) e$n, 0, ATtrue))";
#                $type = "const char *";
                $type = "ATerm";
                $args .= "e$n";
                # !!! in the matcher, we should check that the
                # argument is a string (i.e., a nullary application).
            } elsif ($type eq "int") {
                $args .= "(ATerm) ATmakeInt(e$n)";
            } elsif ($type eq "ATermList" || $type eq "ATermBlob") {
                $args .= "(ATerm) e$n";
            } else {
                $args .= "e$n";
            }
            $formals .= ", " if $formals ne "";
            $formals .= "$type e$n";
            $formals2 .= ", ";
            $formals2 .= "$type & e$n";
            my $m = $n - 1;
            # !!! more checks here
            if ($type eq "int") {
                $unpack .= "    e$n = ATgetInt((ATermInt) ATgetArgument(e, $m));\n";
            } elsif ($type eq "ATermList") {
                $unpack .= "    e$n = (ATermList) ATgetArgument(e, $m);\n";
            } elsif ($type eq "ATermBlob") {
                $unpack .= "    e$n = (ATermBlob) ATgetArgument(e, $m);\n";
            } elsif ($realType eq "string") {
                $unpack .= "    e$n = ATgetArgument(e, $m);\n";
                $unpack .= "    if (ATgetType(e$n) != AT_APPL) return false;\n";
            } else {
                $unpack .= "    e$n = ATgetArgument(e, $m);\n";
            }
            $n++;
        }

        my $arity = scalar @types;

        print HEADER "extern AFun sym$funname;\n\n";
        
        print IMPL "AFun sym$funname = 0;\n";
        
        print HEADER "static inline $result make$funname($formals) {\n";
        if ($arity <= 6) {
            print HEADER "    return (ATerm) ATmakeAppl$arity(sym$funname$args);\n";
        } else {
            $args =~ s/^,//;
            print HEADER "    ATerm array[$arity] = {$args};\n";
            print HEADER "    return (ATerm) ATmakeApplArray(sym$funname, array);\n";
        }
        print HEADER "}\n\n";

        print HEADER "#ifdef __cplusplus\n";
        print HEADER "static inline bool match$funname(ATerm e$formals2) {\n";
        print HEADER "    if (ATgetType(e) != AT_APPL || ATgetAFun(e) != sym$funname) return false;\n";
        print HEADER "$unpack";
        print HEADER "    return true;\n";
        print HEADER "}\n";
        print HEADER "#endif\n\n\n";

        $init .= "    sym$funname = ATmakeAFun(\"$const\", $arity, ATfalse);\n";
        $init .= "    ATprotectAFun(sym$funname);\n";
    }

    elsif (/^\s*(\w+)\s*=\s*(.*)$/) {
        my $name = $1;
        my $value = $2;
        print HEADER "extern ATerm $name;\n";
        print IMPL "ATerm $name = 0;\n";
        $init .= "    $name = $value;\n";
    }

    elsif (/^\s*init\s+(\w+)\s*$/) {
        $initFun = $1;
    }

    else {
        die "bad line: `$_'";
    }
}

print HEADER "void $initFun();\n\n";

print HEADER "static inline const char * aterm2String(ATerm t) {\n";
print HEADER "    return (const char *) ATgetName(ATgetAFun(t));\n";
print HEADER "}\n\n";

print IMPL "\n";
print IMPL "void $initFun() {\n";
print IMPL "$init";
print IMPL "}\n";

close HEADER;
close IMPL;
