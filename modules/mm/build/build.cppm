// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
export module mm.build;

// One import for a consumer. The exported API lives in six partitions,
// listed in the dependency order their imports require: config before
// manifest, manifest before the partitions that consume its model. The
// :detail partition holds helpers shared between implementation units;
// it is not re-exported, so importers of mm.build see exactly this API.
export import :config;
export import :manifest;
export import :platform;
export import :compile;
export import :graph;
export import :external;
