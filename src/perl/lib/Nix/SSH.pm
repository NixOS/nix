package Nix::SSH;

use utf8;
use strict;
use File::Temp qw(tempdir);
use IPC::Open2;

our @ISA = qw(Exporter);
our @EXPORT = qw(
  @globalSshOpts
  readN readInt readString readStrings
  writeInt writeString writeStrings
  connectToRemoteNix
);


our @globalSshOpts = split ' ', ($ENV{"NIX_SSHOPTS"} or "");


sub readN {
    my ($bytes, $from) = @_;
    my $res = "";
    while ($bytes > 0) {
        my $s;
        my $n = sysread($from, $s, $bytes);
        die "I/O error reading from remote side\n" if !defined $n;
        die "got EOF while expecting $bytes bytes from remote side\n" if !$n;
        $bytes -= $n;
        $res .= $s;
    }
    return $res;
}


sub readInt {
    my ($from) = @_;
    return unpack("L<x4", readN(8, $from));
}


sub readString {
    my ($from) = @_;
    my $len = readInt($from);
    my $s = readN($len, $from);
    readN(8 - $len % 8, $from) if $len % 8; # skip padding
    return $s;
}


sub readStrings {
    my ($from) = @_;
    my $n = readInt($from);
    my @res;
    push @res, readString($from) while $n--;
    return @res;
}


sub writeInt {
    my ($n, $to) = @_;
    syswrite($to, pack("L<x4", $n)) or die;
}


sub writeString {
    my ($s, $to) = @_;
    my $len = length $s;
    my $req .= pack("L<x4", $len);
    $req .= $s;
    $req .= "\000" x (8 - $len % 8) if $len % 8;
    syswrite($to, $req) or die;
}


sub writeStrings {
    my ($ss, $to) = @_;
    writeInt(scalar(@{$ss}), $to);
    writeString($_, $to) foreach @{$ss};
}


sub connectToRemoteNix {
    my ($sshHost, $sshOpts, $extraFlags) = @_;

    $extraFlags ||= "";

    # Start ‘nix-store --serve’ on the remote host.
    my ($from, $to);
    # FIXME: don't start a shell, start ssh directly.
    my $pid = open2($from, $to, "exec ssh -x -a $sshHost @globalSshOpts @{$sshOpts} nix-store --serve --write $extraFlags");

    # Do the handshake.
    my $magic;
    eval {
        my $SERVE_MAGIC_1 = 0x390c9deb; # FIXME
        my $clientVersion = 0x200;
        syswrite($to, pack("L<x4L<x4", $SERVE_MAGIC_1, $clientVersion)) or die;
        $magic = readInt($from);
    };
    die "unable to connect to '$sshHost'\n" if $@;
    die "did not get valid handshake from remote host\n" if $magic  != 0x5452eecb;

    my $serverVersion = readInt($from);
    die "unsupported server version\n" if $serverVersion < 0x200 || $serverVersion >= 0x300;

    return ($from, $to, $pid);
}


1;
