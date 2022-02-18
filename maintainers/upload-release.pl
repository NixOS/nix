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

my $isLatest = ($ENV{'IS_LATEST'} // "") eq "1";

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
my $flakeUrl = $evalInfo->{flake} or die;
my $flakeInfo = decode_json(`nix flake metadata --json "$flakeUrl"` or die);
my $nixRev = $flakeInfo->{revision} or die;

my $buildInfo = decode_json(fetch("$evalUrl/job/build.x86_64-linux", 'application/json'));
#print Dumper($buildInfo);

my $releaseName = $buildInfo->{nixname};
$releaseName =~ /nix-(.*)$/ or die;
my $version = $1;

print STDERR "Flake URL is $flakeUrl, Nix revision is $nixRev, version is $version\n";

my $releaseDir = "nix/$releaseName";

my $tmpDir = "$TMPDIR/nix-release/$releaseName";
File::Path::make_path($tmpDir);

my $narCache = "$TMPDIR/nar-cache";
File::Path::make_path($narCache);

my $binaryCache = "https://cache.nixos.org/?local-nar-cache=$narCache";

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
    #print STDERR "$jobName: ", Dumper($buildInfo), "\n";

    my $srcFile = $buildInfo->{buildproducts}->{$productNr}->{path} or die "job '$jobName' lacks product $productNr\n";
    $dstName //= basename($srcFile);
    my $tmpFile = "$tmpDir/$dstName";

    if (!-e $tmpFile) {
        print STDERR "downloading $srcFile to $tmpFile...\n";

        my $fileInfo = decode_json(`NIX_REMOTE=$binaryCache nix store ls --json '$srcFile'`);

        $srcFile = $fileInfo->{target} if $fileInfo->{type} eq 'symlink';

        #print STDERR $srcFile, " ", Dumper($fileInfo), "\n";

        system("NIX_REMOTE=$binaryCache nix store cat '$srcFile' > '$tmpFile'.tmp") == 0
            or die "unable to fetch $srcFile\n";
        rename("$tmpFile.tmp", $tmpFile) or die;
    }

    my $sha256_expected = $buildInfo->{buildproducts}->{$productNr}->{sha256hash};
    my $sha256_actual = `nix hash file --base16 --type sha256 '$tmpFile'`;
    chomp $sha256_actual;
    if (defined($sha256_expected) && $sha256_expected ne $sha256_actual) {
        print STDERR "file $tmpFile is corrupt, got $sha256_actual, expected $sha256_expected\n";
        exit 1;
    }

    write_file("$tmpFile.sha256", $sha256_actual);

    if (! -e "$tmpFile.asc") {
        system("gpg2 --detach-sign --armor $tmpFile") == 0 or die "unable to sign $tmpFile\n";
    }

    return $sha256_expected;
}

downloadFile("binaryTarball.i686-linux", "1");
downloadFile("binaryTarball.x86_64-linux", "1");
downloadFile("binaryTarball.aarch64-linux", "1");
downloadFile("binaryTarball.x86_64-darwin", "1");
downloadFile("binaryTarball.aarch64-darwin", "1");
downloadFile("binaryTarballCross.x86_64-linux.armv6l-linux", "1");
downloadFile("binaryTarballCross.x86_64-linux.armv7l-linux", "1");
downloadFile("installerScript", "1");

# Upload docker images to dockerhub.
my $dockerManifest = "";
my $dockerManifestLatest = "";

for my $platforms (["x86_64-linux", "amd64"], ["aarch64-linux", "arm64"]) {
    my $system = $platforms->[0];
    my $dockerPlatform = $platforms->[1];
    my $fn = "nix-$version-docker-image-$dockerPlatform.tar.gz";
    downloadFile("dockerImage.$system", "1", $fn);

    print STDERR "loading docker image for $dockerPlatform...\n";
    system("docker load -i $tmpDir/$fn") == 0 or die;

    my $tag = "nixos/nix:$version-$dockerPlatform";
    my $latestTag = "nixos/nix:latest-$dockerPlatform";

    print STDERR "tagging $version docker image for $dockerPlatform...\n";
    system("docker tag nix:$version $tag") == 0 or die;

    if ($isLatest) {
        print STDERR "tagging latest docker image for $dockerPlatform...\n";
        system("docker tag nix:$version $latestTag") == 0 or die;
    }

    print STDERR "pushing $version docker image for $dockerPlatform...\n";
    system("docker push -q $tag") == 0 or die;

    if ($isLatest) {
        print STDERR "pushing latest docker image for $dockerPlatform...\n";
        system("docker push -q $latestTag") == 0 or die;
    }

    $dockerManifest .= " --amend $tag";
    $dockerManifestLatest .= " --amend $latestTag"
}

print STDERR "creating multi-platform docker manifest...\n";
system("docker manifest rm nixos/nix:$version");
system("docker manifest create nixos/nix:$version $dockerManifest") == 0 or die;
if ($isLatest) {
    print STDERR "creating latest multi-platform docker manifest...\n";
    system("docker manifest rm nixos/nix:latest");
    system("docker manifest create nixos/nix:latest $dockerManifestLatest") == 0 or die;
}

print STDERR "pushing multi-platform docker manifest...\n";
system("docker manifest push nixos/nix:$version") == 0 or die;

if ($isLatest) {
    print STDERR "pushing latest multi-platform docker manifest...\n";
    system("docker manifest push nixos/nix:latest") == 0 or die;
}

# Upload release files to S3.
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

# Update nix-fallback-paths.nix.
if ($isLatest) {
    system("cd $nixpkgsDir && git pull") == 0 or die;

    sub getStorePath {
        my ($jobName) = @_;
        my $buildInfo = decode_json(fetch("$evalUrl/job/$jobName", 'application/json'));
        return $buildInfo->{buildoutputs}->{out}->{path} or die "cannot get store path for '$jobName'";
    }

    write_file("$nixpkgsDir/nixos/modules/installer/tools/nix-fallback-paths.nix",
               "{\n" .
               "  x86_64-linux = \"" . getStorePath("build.x86_64-linux") . "\";\n" .
               "  i686-linux = \"" . getStorePath("build.i686-linux") . "\";\n" .
               "  aarch64-linux = \"" . getStorePath("build.aarch64-linux") . "\";\n" .
               "  x86_64-darwin = \"" . getStorePath("build.x86_64-darwin") . "\";\n" .
               "  aarch64-darwin = \"" . getStorePath("build.aarch64-darwin") . "\";\n" .
               "}\n");

    system("cd $nixpkgsDir && git commit -a -m 'nix-fallback-paths.nix: Update to $version'") == 0 or die;
}

# Update the "latest" symlink.
$channelsBucket->add_key(
    "nix-latest/install", "",
    { "x-amz-website-redirect-location" => "https://releases.nixos.org/$releaseDir/install" })
    or die $channelsBucket->err . ": " . $channelsBucket->errstr
    if $isLatest;

# Tag the release in Git.
chdir("/home/eelco/Dev/nix-pristine") or die;
system("git remote update origin") == 0 or die;
system("git tag --force --sign $version $nixRev -m 'Tagging release $version'") == 0 or die;
system("git push --tags") == 0 or die;
system("git push --force-with-lease origin $nixRev:refs/heads/latest-release") == 0 or die if $isLatest;
