# Set a PATH (!!! impure).
export PATH=/bin:/usr/bin:$PATH

mkdir $out

cat > $out/substituter <<EOF
#! /bin/sh -ex
echo \$*

case \$* in
    *aaaa*)
        echo "Closure([\"\$2\"],[(\"\$2\",[])])" > \$1
        ;;
    *)
        mkdir \$1
        echo \$3 \$4 > \$1/hello
        ;;
esac        
EOF

chmod +x $out/substituter

