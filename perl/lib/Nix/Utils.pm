package Nix::Utils;

$urlRE = "(?: [a-zA-Z][a-zA-Z0-9\+\-\.]*\:[a-zA-Z0-9\%\/\?\:\@\&\=\+\$\,\-\_\.\!\~\*]+ )";

sub checkURL {
    my ($url) = @_;
    die "invalid URL ‘$url’\n" unless $url =~ /^ $urlRE $ /x;
}
