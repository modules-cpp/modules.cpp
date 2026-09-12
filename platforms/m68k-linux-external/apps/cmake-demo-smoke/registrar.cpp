bool registrar_invoked = false;

namespace {
struct Registrar {
    Registrar() {
        registrar_invoked = true;
    }
};

const Registrar registrar_instance;
}
