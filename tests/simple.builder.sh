echo "PATH=$PATH"

# Verify that the PATH is empty.
if mkdir foo; then exit 1; fi

# Set a PATH (!!! impure).
export PATH=$goodPath

mkdir $out

echo "Hello World!" > $out/hello