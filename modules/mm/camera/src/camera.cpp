// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.camera;
namespace mm::camera {
namespace { Camera unserved; Camera* current = &unserved; }
void set_camera(Camera& camera) { current = &camera; }
Camera& selected_camera() { return *current; }
}
