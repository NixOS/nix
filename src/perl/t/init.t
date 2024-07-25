use strict;
use warnings;
use Test2::V0;

use Nix::Store;

my $s = new Nix::Store("dummy://");

my $res = $s->isValidPath("/nix/store/g1w7hy3qg1w7hy3qg1w7hy3qg1w7hy3q-bar");

ok(!$res, "should not have path");

done_testing;
