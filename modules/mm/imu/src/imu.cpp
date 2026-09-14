// Pawel Wodnicki (C) 2026
// 32bitmicro LLC (C) 2026
module mm.imu;

namespace mm::imu {

namespace {

Imu unserved;
Imu* current = &unserved;

}

void set_imu(Imu& imu) { current = &imu; }

Imu& selected_imu() { return *current; }

}
