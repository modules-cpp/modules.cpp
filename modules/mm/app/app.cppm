// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.app;

export class App {
public:
    App(int argc, char** argv);

    virtual ~App() = default;

    [[nodiscard]] virtual int run() {return 0;};

    [[nodiscard]] int main(int argc, char** argv) {
        return this->run();
    }
};


App::App(int argc, char** argv)
{
    
}


