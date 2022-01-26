#/usr/bin/env bash

# TODO: parse from .version
MAINTENANCE_VERSION="2.6"
VERSION="$MAINTENANCE_VERSION.0"

# Should be override `latest` tag, default true
PUSH_AS_LATEST=1

PLATFORMS="x86_64-linux aarch64-linux"

# ensure we are logged to docker hub
docker login

DOCKER_MANIFEST=""
DOCKER_MANIFEST_LATEST=""

for PLATFORM in $PLATFORMS;
do
  echo "=> Loading docker image for $PLATFORM platform ..."

  DOCKER_IMAGE_TMP_FILE="$PWD/image-$PLATFORM.tar.gz"
  if [ ! -f "$DOCKER_IMAGE_TMP_FILE" ]; then
    curl -L https://hydra.nixos.org/job/nix/maintenance-$MAINTENANCE_VERSION/dockerImage.$PLATFORM/latest/download/1 > $DOCKER_IMAGE_TMP_FILE
  fi
  docker load -i $DOCKER_IMAGE_TMP_FILE

  if   [ "$PLATFORM" = "x86_64-linux" ];  then DOCKER_PLATFORM="amd64"
  elif [ "$PLATFORM" = "aarch64-linux" ]; then DOCKER_PLATFORM="arm64"
  else
    echo "EROROR: No docker platform found for $PLATFORM platform"
    exit 1
  fi

  echo "=> Tagging docker image of version $VERSION for $PLATFORM platform ..."

  docker tag nix:$VERSION nixos/nix:$VERSION-$DOCKER_PLATFORM
  if [ $PUSH_AS_LATEST -eq 1 ]; then
    echo "=> Tagging docker image of version latest for $PLATFORM platform ..."
    docker tag nix:$VERSION nixos/nix:latest-$DOCKER_PLATFORM
  fi

  echo "=> Pushing docker image of version $VERSION for $PLATFORM platform ..."

  docker push nixos/nix:$VERSION-$DOCKER_PLATFORM
  if [ $PUSH_AS_LATEST -eq 1 ]; then
    echo "=> Pushing docker image of version latest for $PLATFORM platform ..."
    docker push nixos/nix:latest-$DOCKER_PLATFORM
  fi

  DOCKER_MANIFEST="$DOCKER_MANIFEST --amend nixos/nix:$VERSION-$DOCKER_PLATFORM"
  DOCKER_MANIFEST_LATEST="$DOCKER_MANIFEST_LATEST --amend nixos/nix:latest-$DOCKER_PLATFORM"

  echo
done

echo "=> Creating $VERSION multi platform docker manifest for the following platforms: $PLATFORMS ..."
docker manifest create nixos/nix:$VERSION $DOCKER_MANIFEST
if [ $PUSH_AS_LATEST -eq 1 ]; then
  echo "=> Creating latest multi platform docker manifest for the following platforms: $PLATFORMS ..."
  docker manifest create nixos/nix:latest $DOCKER_MANIFEST_LATEST
fi

echo "=> Pushing $VERSION multi platform docker manifest ..."
docker manifest push nixos/nix:$VERSION
if [ $PUSH_AS_LATEST -eq 1 ]; then
  echo "=> Pushing latest multi platform docker manifest ..."
  docker manifest push nixos/nix:latest
fi
