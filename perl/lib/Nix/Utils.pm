package Nix::Utils;

our @ISA = qw(Exporter);
our @EXPORT = qw(checkURL uniq writeFile readFile);

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

sub writeFile {
    my ($fn, $s) = @_;
    open TMP, ">$fn" or die;
    print TMP "$s" or die;
    close TMP or die;
}

sub readFile {
    local $/ = undef;
    my ($fn) = @_;
    open TMP, "<$fn" or die;
    my $s = <TMP>;
    close TMP or die;
    return $s;
}
