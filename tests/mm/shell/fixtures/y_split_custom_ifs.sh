count_fields() {
    [ $# -eq 3 ]
}
IFS=":"
var="a:b:c"
count_fields $var
