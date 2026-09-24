test_pos() {
    count=0
    for x; do count=$((count + 1)); done
    [ "$count" -eq 3 ]
}
test_pos a b c
