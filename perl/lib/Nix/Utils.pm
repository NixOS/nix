package Nix::Utils;

$urlRE = "(?: [a-zA-Z][a-zA-Z0-9\+\-\.]*\:[a-zA-Z0-9\%\/\?\:\@\&\=\+\$\,\-\_\.\!\~\*]+ )";

sub checkURL {
    my ($url) = @_;
    die "invalid URL ‘$url’\n" unless $url =~ /^ $urlRE $ /x;
}

sub uniq {
    my %seen;
    my @res;
    foreach my $name (@_) {
        next if $seen{$name};
        $seen{$name} = 1;
        push @res, $name;
    }
    return @res;
}
