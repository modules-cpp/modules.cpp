add() {
    a=$1
    b=$2
    return $((a + b))
}
add 3 4
[ $? -eq 7 ]
