#! /usr/bin/perl -w

if (scalar @ARGV != 1) {
    print "syntax: show-duplication.pl PATH\n";
    exit 1;
}

my $root = $ARGV[0];


my $nameRE = "(?:(?:[A-Za-z0-9\+\_]|(?:-[^0-9]))+)";
my $versionRE = "(?:[A-Za-z0-9\.\-]+)";


my %pkgInstances;


my $pid = open(PATHS, "-|") || exec "nix-store", "-qR", $root;
while (<PATHS>) {
    chomp;
    /^.*\/[0-9a-z]*-(.*)$/;
    my $nameVersion = $1;
    $nameVersion =~ /^($nameRE)(-($versionRE))?$/;
    $name = $1;
    $version = $3;
    $version = "(unnumbered)" unless defined $version;
#    print "$nameVersion $name $version\n";
    push @{$pkgInstances{$name}}, {version => $version, path => $_};
}
close PATHS or exit 1;


sub pathSize {
    my $path = shift;
    my @st = lstat $path or die;

    my $size = $st[7];

    if (-d $path) {
        opendir DIR, $path or die;
        foreach my $name (readdir DIR) {
            next if $name eq "." || $name eq "..";
            $size += pathSize("$path/$name");
        }
    }
    
    return $size;
}


my $totalPaths = 0;
my $totalSize = 0, $totalWaste = 0;

foreach my $name (sort {scalar @{$pkgInstances{$b}} <=> scalar @{$pkgInstances{$a}}} (keys %pkgInstances)) {
    print "$name ", scalar @{$pkgInstances{$name}}, "\n";
    my $allSize = 0;
    foreach my $x (sort {$a->{version} cmp $b->{version}} @{$pkgInstances{$name}}) {
        $totalPaths++;
        my $size = pathSize $x->{path};
        $allSize += $size;
        print "    $x->{version} $size\n";
    }
    my $avgSize = int($allSize / scalar @{$pkgInstances{$name}});
    my $waste = $allSize - $avgSize;
    $totalSize += $allSize;
    $totalWaste += $waste;
    print "    average $avgSize, waste $waste\n";
}


my $avgDupl = $totalPaths / scalar (keys %pkgInstances);
my $wasteFactor = ($totalWaste / $totalSize) * 100;
print "average package duplication $avgDupl, total size $totalSize, total waste $totalWaste, $wasteFactor% wasted\n";
