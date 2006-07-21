mkdir $out

cat > $out/program <<EOF
#! $SHELL
sleep 10000
EOF

chmod +x $out/program
