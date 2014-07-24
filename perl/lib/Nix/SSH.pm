package Nix::SSH;

use strict;
use File::Temp qw(tempdir);
use IPC::Open2;

our @ISA = qw(Exporter);
our @EXPORT = qw(
  sshOpts openSSHConnection closeSSHConnection
  readN readInt readString readStrings
  writeInt writeString writeStrings
  connectToRemoteNix
);


our @sshOpts = split ' ', ($ENV{"NIX_SSHOPTS"} or "");

push @sshOpts, "-x";

my $sshStarted = 0;
my $sshHost;


# Open a master SSH connection to `host', unless there already is a
# running master connection (as determined by `-O check').
sub openSSHConnection {
    my ($host) = @_;
    die if $sshStarted;
    $sshHost = $host;
    return 1 if system("ssh $sshHost @sshOpts -O check 2> /dev/null") == 0;

    my $tmpDir = tempdir("nix-ssh.XXXXXX", CLEANUP => 1, TMPDIR => 1)
        or die "cannot create a temporary directory";

    push @sshOpts, "-S", "$tmpDir/control";

    # Start the master.  We can't use the `-f' flag (fork into
    # background after establishing the connection) because then the
    # child continues to run if we are killed.  So instead make SSH
    # print "started" when it has established the connection, and wait
    # until we see that.
    open SSHPIPE, "ssh $sshHost @sshOpts -M -N -o LocalCommand='echo started' -o PermitLocalCommand=yes |" or die;

    while (<SSHPIPE>) {
        chomp;
        if ($_ eq "started") {
            $sshStarted = 1;
            return 1;
        }
    }

    return 0;
}


# Tell the master SSH client to exit.
sub closeSSHConnection {
    if ($sshStarted) {
        system("ssh $sshHost @sshOpts -O exit 2> /dev/null") == 0
            or warn "unable to stop SSH master: $?";
        $sshStarted = 0;
    }
}


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
    my $pid = open2($from, $to, "exec ssh $sshHost @{$sshOpts} nix-store --serve --write $extraFlags");

    # Do the handshake.
    my $SERVE_MAGIC_1 = 0x390c9deb; # FIXME
    my $clientVersion = 0x200;
    syswrite($to, pack("L<x4L<x4", $SERVE_MAGIC_1, $clientVersion)) or die;
    die "did not get valid handshake from remote host\n" if readInt($from) != 0x5452eecb;
    my $serverVersion = readInt($from);
    die "unsupported server version\n" if $serverVersion < 0x200 || $serverVersion >= 0x300;

    return ($from, $to, $pid);
}


END { my $saved = $?; closeSSHConnection; $? = $saved; }

1;
