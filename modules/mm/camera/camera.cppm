// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;
#include <cstddef>
#include <span>
export module mm.camera;
export namespace mm::camera {
enum class Status { Ok, BadArgument, Unsupported, NotInitialized, Busy, Timeout, TransportError };
struct Geometry { unsigned int width = 0; unsigned int height = 0; };
class Camera {
public:
    virtual ~Camera() = default;
    [[nodiscard]] virtual Geometry geometry() const { return {}; }
    [[nodiscard]] virtual Status initialize() { return Status::Unsupported; }
    [[nodiscard]] virtual Status capture(std::span<std::byte>, unsigned long) {
        return Status::Unsupported;
    }
    [[nodiscard]] virtual Status sleep() { return Status::Unsupported; }
};
void set_camera(Camera& camera);
[[nodiscard]] Camera& selected_camera();
}
