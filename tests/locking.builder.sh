export PATH=/bin:/usr/bin:$PATH

sleep 3

touch $out
# Use `>>'; without proper locking this will cause text duplication.
echo -n $(cat $inputs)$text >> $out

sleep 2