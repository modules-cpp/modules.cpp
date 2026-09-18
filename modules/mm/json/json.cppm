// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.json;

// One import for a consumer. The scanner is exported on its own account: a
// fixed-buffer caller reads with it and never touches a Value.
export import :status;
export import :scan;
export import :value;
