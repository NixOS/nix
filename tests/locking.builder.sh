sleep 3

# Use `>>'; without proper locking this will cause text duplication.
echo -n $(cat $inputs)$text >> $out

sleep 2