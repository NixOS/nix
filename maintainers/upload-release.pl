#! /usr/bin/env nix-shell
#! nix-shell -i perl -p perl perlPackages.LWPUserAgent perlPackages.LWPProtocolHttps perlPackages.FileSlurp perlPackages.NetAmazonS3 gnupg1

use strict;
use Data::Dumper;
use File::Basename;
use File::Path;
use File::Slurp;
use File::Copy;
use JSON::PP;
use LWP::UserAgent;
use Net::Amazon::S3;

my $evalId = $ARGV[0] or die "Usage: $0 EVAL-ID\n";

my $releasesBucketName = "nix-releases";
my $channelsBucketName = "nix-channels";
my $nixpkgsDir = "/home/eelco/Dev/nixpkgs-pristine";

my $TMPDIR = $ENV{'TMPDIR'} // "/tmp";

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

my $releaseDir = "nix/$releaseName";

my $tmpDir = "$TMPDIR/nix-release/$releaseName";
File::Path::make_path($tmpDir);

# S3 setup.
my $aws_access_key_id = $ENV{'AWS_ACCESS_KEY_ID'} or die "No AWS_ACCESS_KEY_ID given.";
my $aws_secret_access_key = $ENV{'AWS_SECRET_ACCESS_KEY'} or die "No AWS_SECRET_ACCESS_KEY given.";

my $s3 = Net::Amazon::S3->new(
    { aws_access_key_id     => $aws_access_key_id,
      aws_secret_access_key => $aws_secret_access_key,
      retry                 => 1,
      host                  => "s3-eu-west-1.amazonaws.com",
    });

my $releasesBucket = $s3->bucket($releasesBucketName) or die;

my $s3_us = Net::Amazon::S3->new(
    { aws_access_key_id     => $aws_access_key_id,
      aws_secret_access_key => $aws_secret_access_key,
      retry                 => 1,
    });

my $channelsBucket = $s3_us->bucket($channelsBucketName) or die;

sub downloadFile {
    my ($jobName, $productNr, $dstName) = @_;

    my $buildInfo = decode_json(fetch("$evalUrl/job/$jobName", 'application/json'));

    my $srcFile = $buildInfo->{buildproducts}->{$productNr}->{path} or die "job '$jobName' lacks product $productNr\n";
    $dstName //= basename($srcFile);
    my $tmpFile = "$tmpDir/$dstName";

    if (!-e $tmpFile) {
        print STDERR "downloading $srcFile to $tmpFile...\n";
        system("NIX_REMOTE=https://cache.nixos.org/ nix cat-store '$srcFile' > '$tmpFile'") == 0
            or die "unable to fetch $srcFile\n";
    }

    my $sha256_expected = $buildInfo->{buildproducts}->{$productNr}->{sha256hash} or die;
    my $sha256_actual = `nix hash-file --base16 --type sha256 '$tmpFile'`;
    chomp $sha256_actual;
    if ($sha256_expected ne $sha256_actual) {
        print STDERR "file $tmpFile is corrupt, got $sha256_actual, expected $sha256_expected\n";
        exit 1;
    }

    write_file("$tmpFile.sha256", $sha256_expected);

    if (! -e "$tmpFile.asc") {
        system("gpg2 --detach-sign --armor $tmpFile") == 0 or die "unable to sign $tmpFile\n";
    }

    return $sha256_expected;
}

downloadFile("tarball", "2"); # .tar.bz2
my $tarballHash = downloadFile("tarball", "3"); # .tar.xz
downloadFile("binaryTarball.i686-linux", "1");
downloadFile("binaryTarball.x86_64-linux", "1");
downloadFile("binaryTarball.aarch64-linux", "1");
downloadFile("binaryTarball.x86_64-darwin", "1");
downloadFile("installerScript", "1");

for my $fn (glob "$tmpDir/*") {
    my $name = basename($fn);
    my $dstKey = "$releaseDir/" . $name;
    unless (defined $releasesBucket->head_key($dstKey)) {
        print STDERR "uploading $fn to s3://$releasesBucketName/$dstKey...\n";

        my $configuration = ();
        $configuration->{content_type} = "application/octet-stream";

        if ($fn =~ /.sha256|.asc|install/) {
            # Text files
            $configuration->{content_type} = "text/plain";
        }

        $releasesBucket->add_key_filename($dstKey, $fn, $configuration)
            or die $releasesBucket->err . ": " . $releasesBucket->errstr;
    }
}

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
    for my $product (values %{$buildInfo->{buildproducts}}) {
        next unless $product->{type} eq "nix-build";
        next if $product->{path} =~ /[a-z]+$/;
        return $product->{path};
    }
    die;
}

write_file("$nixpkgsDir/nixos/modules/installer/tools/nix-fallback-paths.nix",
           "{\n" .
           "  x86_64-linux = \"" . getStorePath("build.x86_64-linux") . "\";\n" .
           "  i686-linux = \"" . getStorePath("build.i686-linux") . "\";\n" .
           "  aarch64-linux = \"" . getStorePath("build.aarch64-linux") . "\";\n" .
           "  x86_64-darwin = \"" . getStorePath("build.x86_64-darwin") . "\";\n" .
           "}\n");

system("cd $nixpkgsDir && git commit -a -m 'nix: $oldName -> $version'") == 0 or die;

# Update the "latest" symlink.
$channelsBucket->add_key(
    "nix-latest/install", "",
    { "x-amz-website-redirect-location" => "https://releases.nixos.org/$releaseDir/install" })
    or die $channelsBucket->err . ": " . $channelsBucket->errstr;

# Tag the release in Git.
chdir("/home/eelco/Dev/nix-pristine") or die;
system("git remote update origin") == 0 or die;
system("git tag --force --sign $version $nixRev -m 'Tagging release $version'") == 0 or die;
system("git push --tags") == 0 or die;
system("git push --force-with-lease origin $nixRev:refs/heads/latest-release") == 0 or die;
