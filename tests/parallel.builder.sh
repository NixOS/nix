echo "DOING $text"


export PATH=/bin:/usr/bin:$PATH


# increase counter
while ! ln -s x $shared.lock; do
    sleep 1
done
test -f $shared.cur || echo 0 > $shared.cur
test -f $shared.max || echo 0 > $shared.max
new=$(expr $(cat $shared.cur) + 1)
if test $new -gt $(cat $shared.max); then
    echo $new > $shared.max
fi
echo $new > $shared.cur
rm $shared.lock


echo -n $(cat $inputs)$text > $out

sleep 3


# decrease counter
while ! ln -s x $shared.lock; do
    sleep 1
done
test -f $shared.cur || echo 0 > $shared.cur
echo $(expr $(cat $shared.cur) - 1) > $shared.cur
rm $shared.lock
