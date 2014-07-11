package Nix::CopyClosure;

use strict;
use Nix::Config;
use Nix::Store;
use List::Util qw(sum);
use IPC::Open2;


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


sub writeString {
    my ($s, $to) = @_;
    my $len = length $s;
    my $req .= pack("L<x4", $len);
    $req .= $s;
    $req .= "\000" x (8 - $len % 8) if $len % 8;
    syswrite($to, $req) or die;
}


sub copyTo {
    my ($sshHost, $sshOpts, $storePaths, $compressor, $decompressor,
        $includeOutputs, $dryRun, $sign, $progressViewer, $useSubstitutes) = @_;

    $useSubstitutes = 0 if $dryRun;

    # Get the closure of this path.
    my @closure = reverse(topoSortPaths(computeFSClosure(0, $includeOutputs,
        map { followLinksToStorePath $_ } @{$storePaths})));

    # Start ‘nix-store --serve’ on the remote host.
    my ($from, $to);
    my $pid = open2($from, $to, "ssh $sshHost @{$sshOpts} nix-store --serve --write");

    # Do the handshake.
    eval {
        my $SERVE_MAGIC_1 = 0x390c9deb; # FIXME
        my $clientVersion = 0x200;
        syswrite($to, pack("L<x4L<x4", $SERVE_MAGIC_1, $clientVersion)) or die;
        die "did not get valid handshake from remote host\n" if readInt($from) != 0x5452eecb;
        my $serverVersion = readInt($from);
        die "unsupported server version\n" if $serverVersion < 0x200 || $serverVersion >= 0x300;
    };
    if ($@) {
        chomp $@;
        warn "$@; falling back to old closure copying method\n";
        return oldCopyTo(\@closure, @_);
    }

    # Send the "query valid paths" command with the "lock" option
    # enabled. This prevents a race where the remote host
    # garbage-collect paths that are already there. Optionally, ask
    # the remote host to substitute missing paths.
    syswrite($to, pack("L<x4L<x4L<x4L<x4", 1, 1, $useSubstitutes, scalar @closure)) or die;
    writeString($_, $to) foreach @closure;

    # Get back the set of paths that are already valid on the remote host.
    my %present;
    my $n = readInt($from);
    while ($n--) {
        my $len = readInt($from);
        my $s = readN($len, $from);
        $present{$s} = 1;
        readN(8 - $len % 8, $from) if $len % 8; # skip padding
    }

    my @missing = grep { !$present{$_} } @closure;
    return if !@missing;

    my $missingSize = 0;
    $missingSize += (queryPathInfo($_, 1))[3] foreach @missing;

    printf STDERR "copying %d missing paths (%.2f MiB) to ‘$sshHost’...\n",
        scalar(@missing), $missingSize / (1024**2);
    return if $dryRun;

    # Send the "import paths" command.
    syswrite($to, pack("L<x4", 4)) or die;
    writeString($compressor, $to);

    if ($compressor || $progressViewer) {

        # Compute the size of the closure for the progress viewer.
        $progressViewer = "$progressViewer -s $missingSize" if $progressViewer;

        # Start the compressor and/or progress viewer in between us
        # and the remote host.
        my $to_;
        my $pid2 = open2(">&" . fileno($to), $to_,
            $progressViewer && $compressor ? "$progressViewer | $compressor" : $progressViewer || $compressor);
        close $to;
        exportPaths(fileno($to_), $sign, @missing);
        close $to_;
        waitpid $pid2, 0;

    } else {
        exportPaths(fileno($to), $sign, @missing);
        close $to;
    }

    readInt($from) == 1 or die "remote machine \`$sshHost' failed to import closure\n";
}


# For backwards compatibility with Nix <= 1.7. Will be removed
# eventually.
sub oldCopyTo {
    my ($closure, $sshHost, $sshOpts, $storePaths, $compressor, $decompressor,
        $includeOutputs, $dryRun, $sign, $progressViewer, $useSubstitutes) = @_;

    # Optionally use substitutes on the remote host.
    if (!$dryRun && $useSubstitutes) {
        system "ssh $sshHost @{$sshOpts} nix-store -r --ignore-unknown @$closure";
        # Ignore exit status because this is just an optimisation.
    }

    # Ask the remote host which paths are invalid.  Because of limits
    # to the command line length, do this in chunks.  Eventually,
    # we'll want to use ‘--from-stdin’, but we can't rely on the
    # target having this option yet.
    my @missing;
    my $missingSize = 0;
    while (scalar(@$closure) > 0) {
        my @ps = splice(@$closure, 0, 1500);
        open(READ, "set -f; ssh $sshHost @{$sshOpts} nix-store --check-validity --print-invalid @ps|");
        while (<READ>) {
            chomp;
            push @missing, $_;
            my ($deriver, $narHash, $time, $narSize, $refs) = queryPathInfo($_, 1);
            $missingSize += $narSize;
        }
        close READ or die;
    }

    $compressor = "$compressor |" if $compressor ne "";
    $decompressor = "$decompressor |" if $decompressor ne "";
    $progressViewer = "$progressViewer -s $missingSize |" if $progressViewer ne "";

    # Export the store paths and import them on the remote machine.
    if (scalar @missing > 0) {
        print STDERR "copying ", scalar @missing, " missing paths to ‘$sshHost’...\n";
        unless ($dryRun) {
            open SSH, "| $progressViewer $compressor ssh $sshHost @{$sshOpts} '$decompressor nix-store --import' > /dev/null" or die;
            exportPaths(fileno(SSH), $sign, @missing);
            close SSH or die "copying store paths to remote machine `$sshHost' failed: $?";
        }
    }
}


1;
