extern bool registrar_invoked;

int main() {
    return registrar_invoked ? 0 : 1;
}
