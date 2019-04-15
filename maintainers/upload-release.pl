#! /usr/bin/env nix-shell
#! nix-shell -i perl -p perl perlPackages.LWPUserAgent perlPackages.LWPProtocolHttps perlPackages.FileSlurp gnupg1

use strict;
use Data::Dumper;
use File::Basename;
use File::Path;
use File::Slurp;
use File::Copy;
use JSON::PP;
use LWP::UserAgent;

my $evalId = $ARGV[0] or die "Usage: $0 EVAL-ID\n";

my $releasesDir = "/home/eelco/mnt/releases";
my $nixpkgsDir = "/home/eelco/Dev/nixpkgs-pristine";

# FIXME: cut&paste from nixos-channel-scripts.
sub fetch {
    my ($url, $type) = @_;

    my $ua = LWP::UserAgent->new;
    $ua->default_header('Accept', $type) if defined $type;

    my $response = $ua->get($url);
    die "could not download $url: ", $response->status_line, "\n" unless $response->is_success;

    return $response->decoded_content;
}

my $evalUrl = "https://hydra.nixos.org/eval/$evalId";
my $evalInfo = decode_json(fetch($evalUrl, 'application/json'));
#print Dumper($evalInfo);

my $nixRev = $evalInfo->{jobsetevalinputs}->{nix}->{revision} or die;

my $tarballInfo = decode_json(fetch("$evalUrl/job/tarball", 'application/json'));

my $releaseName = $tarballInfo->{releasename};
$releaseName =~ /nix-(.*)$/ or die;
my $version = $1;

print STDERR "Nix revision is $nixRev, version is $version\n";

File::Path::make_path($releasesDir);
if (system("mountpoint -q $releasesDir") != 0) {
    system("sshfs hydra-mirror:/releases $releasesDir") == 0 or die;
}

my $releaseDir = "$releasesDir/nix/$releaseName";
File::Path::make_path($releaseDir);

sub downloadFile {
    my ($jobName, $productNr, $dstName) = @_;

    my $buildInfo = decode_json(fetch("$evalUrl/job/$jobName", 'application/json'));

    my $srcFile = $buildInfo->{buildproducts}->{$productNr}->{path} or die "job '$jobName' lacks product $productNr\n";
    $dstName //= basename($srcFile);
    my $dstFile = "$releaseDir/" . $dstName;

    if (! -e $dstFile) {
        print STDERR "downloading $srcFile to $dstFile...\n";
        system("NIX_REMOTE=https://cache.nixos.org/ nix cat-store '$srcFile' > '$dstFile.tmp'") == 0
            or die "unable to fetch $srcFile\n";
        rename("$dstFile.tmp", $dstFile) or die;
    }

    my $sha256_expected = $buildInfo->{buildproducts}->{$productNr}->{sha256hash} or die;
    my $sha256_actual = `nix hash-file --base16 --type sha256 '$dstFile'`;
    chomp $sha256_actual;
    if ($sha256_expected ne $sha256_actual) {
        print STDERR "file $dstFile is corrupt, got $sha256_actual, expected $sha256_expected\n";
        exit 1;
    }

    write_file("$dstFile.sha256", $sha256_expected);

    if (! -e "$dstFile.asc") {
        system("gpg2 --detach-sign --armor $dstFile") == 0 or die "unable to sign $dstFile\n";
    }

    return ($dstFile, $sha256_expected);
}

downloadFile("tarball", "2"); # .tar.bz2
my ($tarball, $tarballHash) = downloadFile("tarball", "3"); # .tar.xz
downloadFile("binaryTarball.i686-linux", "1");
downloadFile("binaryTarball.x86_64-linux", "1");
downloadFile("binaryTarball.aarch64-linux", "1");
downloadFile("binaryTarball.x86_64-darwin", "1");
downloadFile("installerScript", "1");

exit if $version =~ /pre/;

# Update Nixpkgs in a very hacky way.
system("cd $nixpkgsDir && git pull") == 0 or die;
my $oldName = `nix-instantiate --eval $nixpkgsDir -A nix.name`; chomp $oldName;
my $oldHash = `nix-instantiate --eval $nixpkgsDir -A nix.src.outputHash`; chomp $oldHash;
print STDERR "old stable version in Nixpkgs = $oldName / $oldHash\n";

my $fn = "$nixpkgsDir/pkgs/tools/package-management/nix/default.nix";
my $oldFile = read_file($fn);
$oldFile =~ s/$oldName/"$releaseName"/g;
$oldFile =~ s/$oldHash/"$tarballHash"/g;
write_file($fn, $oldFile);

$oldName =~ s/nix-//g;
$oldName =~ s/"//g;

sub getStorePath {
    my ($jobName) = @_;
    my $buildInfo = decode_json(fetch("$evalUrl/job/$jobName", 'application/json'));
    die unless $buildInfo->{buildproducts}->{1}->{type} eq "nix-build";
    return $buildInfo->{buildproducts}->{1}->{path};
}

write_file("$nixpkgsDir/nixos/modules/installer/tools/nix-fallback-paths.nix",
           "{\n" .
           "  x86_64-linux = \"" . getStorePath("build.x86_64-linux") . "\";\n" .
           "  i686-linux = \"" . getStorePath("build.i686-linux") . "\";\n" .
           "  aarch64-linux = \"" . getStorePath("build.aarch64-linux") . "\";\n" .
           "  x86_64-darwin = \"" . getStorePath("build.x86_64-darwin") . "\";\n" .
           "}\n");

system("cd $nixpkgsDir && git commit -a -m 'nix: $oldName -> $version'") == 0 or die;

# Extract the HTML manual.
File::Path::make_path("$releaseDir/manual");

system("tar xvf $tarball --strip-components=3 -C $releaseDir/manual --wildcards '*/doc/manual/*.html' '*/doc/manual/*.css' '*/doc/manual/*.gif' '*/doc/manual/*.png'") == 0 or die;

if (! -e "$releaseDir/manual/index.html") {
    symlink("manual.html", "$releaseDir/manual/index.html") or die;
}

# Update the "latest" symlink.
symlink("$releaseName", "$releasesDir/nix/latest-tmp") or die;
rename("$releasesDir/nix/latest-tmp", "$releasesDir/nix/latest") or die;

# Tag the release in Git.
chdir("/home/eelco/Dev/nix-pristine") or die;
system("git remote update origin") == 0 or die;
system("git tag --force --sign $version $nixRev -m 'Tagging release $version'") == 0 or die;

# Update the website.
my $siteDir = "/home/eelco/Dev/nixos-homepage-pristine";

system("cd $siteDir && git pull") == 0 or die;

write_file("$siteDir/nix-release.tt",
           "[%-\n" .
           "latestNixVersion = \"$version\"\n" .
           "-%]\n");

system("cd $siteDir && git commit -a -m 'Nix $version released'") == 0 or die;
