source common.sh

################################################################################
## Check that the ipfs daemon is enabled in your environment
################################################################################

# To see if ipfs is connected to the network, we check if we can see some peers
# other than ourselves.
NPEERS=$(ipfs swarm peers | wc -l)
echo $NPEERS
if (( $NPEERS < 2 )); then
    echo "The ipfs daemon doesn't seem to be enabled (can't find peers)"
    exit 1
fi

################################################################################
## Create the folders for the source and destination stores
################################################################################

IPFS_TESTS=$TEST_ROOT/ipfs_tests

# Here we define some store locations, one for the initial store we upload, and
# the other three for the destination stores to which we'll copy (one for each
# method)
IPFS_SRC_STORE=$IPFS_TESTS/ipfs_source_store

IPFS_DST_HTTP_STORE=$IPFS_TESTS/ipfs_dest_http_store
IPFS_DST_HTTP_LOCAL_STORE=$IPFS_TESTS/ipfs_dest_http_local_store
IPFS_DST_IPFS_STORE=$IPFS_TESTS/ipfs_dest_ipfs_store
IPFS_DST_IPNS_STORE=$IPFS_TESTS/ipfs_dest_ipns_store

################################################################################
## Generate the keys to sign the store
################################################################################

SIGNING_KEY_NAME='nixcache.for.ipfs-1'
SIGNING_KEY_PRI_FILE='nix-cache-key.sec'
SIGNING_KEY_PUB_FILE='nix-cache-key.pub'

nix-store --generate-binary-cache-key $SIGNING_KEY_NAME $SIGNING_KEY_PRI_FILE $SIGNING_KEY_PUB_FILE

################################################################################
## Create, sign and upload the source store
################################################################################

mkdir -p $IPFS_SRC_STORE
BUILD_COMMAND='nix-build "<nixpkgs>" -A hello.src'

nix copy --to file://$IPFS_SRC_STORE $($BUILD_COMMAND)

nix sign-paths --store file://$IPFS_SRC_STORE \
    -k ~/nix-cache-key.sec \
    $($BUILD_COMMAND) -r

IPFS_HASH=$(ipfs add -r $IPFS_SRC_STORE 2>/dev/null | tail -n 1 | awk '{print $2}')


# ################################################################################
# ## Create the http store and download the derivation there
# ################################################################################

# mkdir IPFS_DST_HTTP_STORE

# IPFS_HTTP_PREFIX='https://gateway.ipfs.io/ipfs'


# DOWNLOAD_LOCATION=$(NIX_REMOTE=local $BUILD_COMMAND \
# --option substituters $IPFS_HTTP_PREFIX/$IPFS_HASH \
# --store $IPFS_DST_HTTP_STORE \
# --option trusted-public-keys $(cat $SIGNING_KEY_PUB_FILE) \
# | tail -2 | head -1 | awk '{print $5}')

# ### TODO
# ### Check that the download location coincides

# ################################################################################
# ## Create the local http store and download the derivation there
# ################################################################################

# mkdir IPFS_DST_HTTP_LOCAL_STORE

# IPFS_HTTP_LOCAL_PREFIX='http://localhost:8080/ipfs'

# DOWNLOAD_LOCATION=$(NIX_REMOTE=local $BUILD_COMMAND \
# --option substituters $IPFS_HTTP_LOCAL_PREFIX/$IPFS_HASH \
# --store $IPFS_DST_HTTP_LOCAL_STORE \
# --option trusted-public-keys $(cat $SIGNING_KEY_PUB_FILE) \
# | tail -2 | head -1 | awk '{print $5}')

# ### TODO
# ### Check that the download location coincides

# ################################################################################
# ## Create the ipfs store and download the derivation there
# ################################################################################

# mkdir IPFS_DST_IPFS_STORE

# IPFS_IPFS_PREFIX='/ipfs'

# DOWNLOAD_LOCATION=$(NIX_REMOTE=local $BUILD_COMMAND \
# --option substituters $IPFS_IPFS_PREFIX/$IPFS_HASH \
# --store $IPFS_DST_IPFS_STORE \
# --option trusted-public-keys $(cat $SIGNING_KEY_PUB_FILE) \
# | tail -2 | head -1 | awk '{print $5}')

# ### TODO
# ### Check that the download location coincides

# ################################################################################
# ## Create the ipns store and download the derivation there
# ################################################################################

# # First I have to publish:
# IPNS_ID=$(ipfs name publish $IPFS_HASH | awk '{print substr($3,1,length($3)-1)}')

# mkdir IPFS_DST_IPNS_STORE
# IPFS_IPNS_PREFIX='/ipns'

# DOWNLOAD_LOCATION=$(NIX_REMOTE=local $BUILD_COMMAND \
# --option substituters $IPFS_IPNS_PREFIX/$IPNS_ID \
# --store $IPFS_DST_IPFS_STORE \
# --option trusted-public-keys $(cat $SIGNING_KEY_PUB_FILE) \
# | tail -2 | head -1 | awk '{print $5}')

# ### TODO
# ### Check that the download location coincides

################################################################################
## Cleanup
################################################################################

# Delete the keys used to sign the store
rm $SIGNING_KEY_PRI_FILE $SIGNING_KEY_PUB_FILE

# Remove all the stores
rm -rf $IPFS_SRC_STORE \
    $IPFS_DST_HTTP_STORE $IPFS_DST_HTTP_LOCAL_STORE \
    $IPFS_DST_IPFS_STORE $IPFS_DST_IPNS_STORE

# Remove the result of the build
rm result


exit 1
