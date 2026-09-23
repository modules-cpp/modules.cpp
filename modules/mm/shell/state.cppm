// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;

export module mm.shell:state;

export namespace mm::shell {

// Level-1 minimal shell state representation.
// Variable and parameter storage will be attached in Change Set 6.
struct ShellState {
    int last_status = 0;
};

}  // namespace mm::shell
