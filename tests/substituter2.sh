# Set a PATH (!!! impure).
export PATH=/bin:/usr/bin:$PATH

mkdir $out

cat > $out/substituter <<EOF
#! /bin/sh -ex
echo \$*

case \$* in
    *)
        exit 1
        ;;
esac        
EOF

chmod +x $out/substituter

