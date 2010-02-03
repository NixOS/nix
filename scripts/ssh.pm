use strict;
use File::Temp qw(tempdir);

our @sshOpts = split ' ', ($ENV{"NIX_SSHOPTS"} or "");

my $sshStarted = 0;
my $sshHost;

# Open a master SSH connection to `host', unless there already is a
# running master connection (as determined by `-O check').
sub openSSHConnection {
    my ($host) = @_;
    die if $sshStarted;
    $sshHost = $host;
    return if system("ssh $sshHost @sshOpts -O check 2> /dev/null") == 0;

    my $tmpDir = tempdir("nix-ssh.XXXXXX", CLEANUP => 1, TMPDIR => 1)
        or die "cannot create a temporary directory";
    
    push @sshOpts, "-S", "$tmpDir/control";
    system("ssh $sshHost @sshOpts -M -N -f") == 0
        or die "unable to start SSH: $?";
    $sshStarted = 1;
}

# Tell the master SSH client to exit.
sub closeSSHConnection {
    if ($sshStarted) {
        system("ssh $sshHost @sshOpts -O exit 2> /dev/null") == 0
            or warn "unable to stop SSH master: $?";
    }
}

END { closeSSHConnection; }

return 1;
