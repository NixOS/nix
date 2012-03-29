package Nix::CopyClosure;

use strict;
use Nix::Config;
use Nix::Store;


sub copyTo {
    my ($sshHost, $sshOpts, $storePaths, $compressor, $decompressor, $includeOutputs, $dryRun, $sign, $progressViewer) = @_;

    $compressor = "$compressor |" if $compressor ne "";
    $decompressor = "$decompressor |" if $decompressor ne "";
    $progressViewer = "$progressViewer |" if $progressViewer ne "";

    # Get the closure of this path.
    my @closure = reverse(topoSortPaths(computeFSClosure(0, $includeOutputs,
        map { followLinksToStorePath $_ } @{$storePaths})));

    # Ask the remote host which paths are invalid.  Because of limits
    # to the command line length, do this in chunks.  Eventually,
    # we'll want to use ‘--from-stdin’, but we can't rely on the
    # target having this option yet.
    my @missing = ();
    while (scalar(@closure) > 0) {
        my @ps = splice(@closure, 0, 1500);
        open(READ, "set -f; ssh $sshHost @{$sshOpts} nix-store --check-validity --print-invalid @ps|");
        while (<READ>) {
            chomp;
            push @missing, $_;
        }
        close READ or die;
    }

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
