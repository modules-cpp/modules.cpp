rec() {
    if [ "$1" -eq 0 ]; then
        return 42
    fi
    next=$1
    next=$((next - 1))
    rec $next
    return $?
}
rec 3
[ $? -eq 42 ]
