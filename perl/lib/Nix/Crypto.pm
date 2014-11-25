package Nix::Crypto;

use strict;
use MIME::Base64;
use Nix::Store;
use Nix::Config;
use IPC::Open2;

our @ISA = qw(Exporter);
our @EXPORT = qw(signString isValidSignature);

sub signString {
    my ($privateKeyFile, $s) = @_;
    my $hash = hashString("sha256", 0, $s);
    my ($from, $to);
    my $pid = open2($from, $to, $Nix::Config::openssl, "rsautl", "-sign", "-inkey", $privateKeyFile);
    print $to $hash;
    close $to;
    local $/ = undef;
    my $sig = <$from>;
    close $from;
    waitpid($pid, 0);
    die "$0: OpenSSL returned exit code $? while signing hash\n" if $? != 0;
    my $sig64 = encode_base64($sig, "");
    return $sig64;
}

sub isValidSignature {
    my ($publicKeyFile, $sig64, $s) = @_;
    my ($from, $to);
    my $pid = open2($from, $to, $Nix::Config::openssl, "rsautl", "-verify", "-inkey", $publicKeyFile, "-pubin");
    print $to decode_base64($sig64);
    close $to;
    my $decoded = <$from>;
    close $from;
    waitpid($pid, 0);
    return 0 if $? != 0;
    my $hash = hashString("sha256", 0, $s);
    return $decoded eq $hash;
}

1;
