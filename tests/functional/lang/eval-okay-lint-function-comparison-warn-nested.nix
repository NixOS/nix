# Distinct function values inside lists are not pointer-equal,
# so comparison reaches the function case and warns.
[ (x: x) ] == [ (x: x) ]
