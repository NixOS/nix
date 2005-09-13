export PATH=/bin:/usr/bin:$PATH

env

sleep 3

touch $out
echo CAT1
cat $inputs
echo CAT2
echo -n $(cat $inputs)$text
echo CAT3


# Use `>>'; without proper locking this will cause text duplication.
echo -n $(cat $inputs)$text >> $out

sleep 2