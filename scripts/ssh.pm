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
    return 1 if system("ssh $sshHost @sshOpts -O check 2> /dev/null") == 0;

    my $tmpDir = tempdir("nix-ssh.XXXXXX", CLEANUP => 1, TMPDIR => 1)
        or die "cannot create a temporary directory";
    
    push @sshOpts, "-S", "$tmpDir/control";

    # Start the master.  We can't use the `-f' flag (fork into
    # background after establishing the connection) because then the
    # child continues to run if we are killed.  So instead make SSH
    # print "started" when it has established the connection, and wait
    # until we see that.
    open SSH, "ssh $sshHost @sshOpts -M -N -o LocalCommand='echo started' -o PermitLocalCommand=yes |" or die;
    while (<SSH>) {
        chomp;
        last if /started/;
    }
    
    $sshStarted = 1;
    return 1;
}

# Tell the master SSH client to exit.
sub closeSSHConnection {
    if ($sshStarted) {
        system("ssh $sshHost @sshOpts -O exit 2> /dev/null") == 0
            or warn "unable to stop SSH master: $?";
    }
}

END { my $saved = $?; closeSSHConnection; $? = $saved; }

return 1;
