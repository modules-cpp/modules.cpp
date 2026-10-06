// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module;
#include <cstddef>
#include <span>
#include "camera-cxx.h"
export module platform.pico_cam_a.camera;
import mm.camera;
import mm.mcu;
namespace {
class Camera final : public mm::camera::Camera {
public:
    mm::camera::Geometry geometry() const override { return {324, 244}; }
    mm::camera::Status initialize() override {
        return static_cast<mm::camera::Status>(mm_pico_cam_initialize());
    }
    mm::camera::Status capture(std::span<std::byte> data, unsigned long timeout_ms) override {
        return static_cast<mm::camera::Status>(mm_pico_cam_capture(
            reinterpret_cast<unsigned char*>(data.data()), data.size(), timeout_ms));
    }
    mm::camera::Status sleep() override {
        return static_cast<mm::camera::Status>(mm_pico_cam_sleep());
    }
};
Camera camera;
struct Register { Register() { mm::camera::set_camera(camera); } };
const Register registered;
}
