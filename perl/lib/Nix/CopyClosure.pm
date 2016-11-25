package Nix::CopyClosure;

use utf8;
use strict;
use Nix::Config;
use Nix::Store;
use Nix::SSH;
use List::Util qw(sum);
use IPC::Open2;


sub copyToOpen {
    my ($from, $to, $sshHost, $storePaths, $includeOutputs, $dryRun, $useSubstitutes) = @_;

    $useSubstitutes = 0 if $dryRun || !defined $useSubstitutes;

    # Get the closure of this path.
    my @closure = reverse(topoSortPaths(computeFSClosure(0, $includeOutputs,
        map { followLinksToStorePath $_ } @{$storePaths})));

    # Send the "query valid paths" command with the "lock" option
    # enabled. This prevents a race where the remote host
    # garbage-collect paths that are already there. Optionally, ask
    # the remote host to substitute missing paths.
    syswrite($to, pack("L<x4L<x4L<x4", 1, 1, $useSubstitutes)) or die;
    writeStrings(\@closure, $to);

    # Get back the set of paths that are already valid on the remote host.
    my %present;
    $present{$_} = 1 foreach readStrings($from);

    my @missing = grep { !$present{$_} } @closure;
    return if !@missing;

    my $missingSize = 0;
    $missingSize += (queryPathInfo($_, 1))[3] foreach @missing;

    printf STDERR "copying %d missing paths (%.2f MiB) to '$sshHost'...\n",
        scalar(@missing), $missingSize / (1024**2);
    return if $dryRun;

    # Send the "import paths" command.
    syswrite($to, pack("L<x4", 4)) or die;
    exportPaths(fileno($to), @missing);
    readInt($from) == 1 or die "remote machine '$sshHost' failed to import closure\n";
}


sub copyTo {
    my ($sshHost, $storePaths, $includeOutputs, $dryRun, $useSubstitutes) = @_;

    # Connect to the remote host.
    my ($from, $to) = connectToRemoteNix($sshHost, []);

    copyToOpen($from, $to, $sshHost, $storePaths, $includeOutputs, $dryRun, $useSubstitutes);

    close $to;
}


1;
