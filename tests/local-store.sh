source common.sh

cd $TEST_ROOT

echo example > example.txt
mkdir -p ./x

NIX_STORE_DIR=$TEST_ROOT/x

CORRECT_PATH=$(nix-store --store ./x --add example.txt)

PATH1=$(nix path-info --store ./x $CORRECT_PATH)
[ $CORRECT_PATH == $PATH1 ]

PATH2=$(nix path-info --store "$PWD/x" $CORRECT_PATH)
[ $CORRECT_PATH == $PATH2 ]

# FIXME we could also test the query parameter version:
# PATH3=$(nix path-info --store "local?store=$PWD/x" $CORRECT_PATH)
# [ $CORRECT_PATH == $PATH3 ]
