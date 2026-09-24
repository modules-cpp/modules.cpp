sum=0
for x in 1 2 3; do sum=$((sum+x)); done
[ "$sum" -eq 6 ]
