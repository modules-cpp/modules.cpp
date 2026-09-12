// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
//
// The C++ side of the mm.mcu platform ABI. mcu-c.h is plain C so a board or a
// vendor adapter can include it directly; this shim supplies the language linkage
// the C++ side needs, because guarding the header itself on __cplusplus would
// require a preprocessor directive the specification does not permit.
extern "C" {
#include "mcu-c.h"
}
