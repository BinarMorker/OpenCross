// Debug free camera pose maths (game/free_cam.h): seeding from a shot, look clamps, movement directions, switch-on / reset / hide gating.
#include <cmath>
#include <cstdio>

#include "game/free_cam.h"

using oc::game::FreeCam;

namespace {
int failures = 0;
#define CHECK(x)                                              \
  do {                                                        \
    if (!(x)) {                                               \
      std::printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #x); \
      ++failures;                                             \
    }                                                         \
  } while (0)
bool near(float lhs, float rhs, float tolerance = 1e-3f) { return std::fabs(lhs - rhs) <= tolerance; }
}  // namespace

int main() {
  {  // seeding: the pose looks from eye at target
    FreeCam cam;
    CHECK(!cam.on() && !cam.hide_overlays());
    cam.set_on(true);
    CHECK(cam.needs_init());
    // Straight down +z.
    cam.init_from({100, 200, -300}, {100, 200, 700});
    CHECK(!cam.needs_init());
    CHECK(near(cam.yaw(), 0) && near(cam.pitch(), 0));
    CHECK(near(cam.target()[2], 700) && near(cam.target()[0], 100));
    // Toward +x: yaw +90 degrees.
    cam.init_from({0, 0, 0}, {1000, 0, 0});
    CHECK(near(cam.yaw(), FreeCam::kPi / 2));
    // Straight up.
    cam.init_from({0, 0, 0}, {0, 500, 0});
    CHECK(near(cam.pitch(), FreeCam::kPi / 2));
  }
  {  // look: yaw wraps, pitch clamps below the zenith
    FreeCam cam;
    cam.set_on(true);
    cam.init_from({0, 0, 0}, {0, 0, 1});
    cam.look(FreeCam::kPi * 1.5f, 5.0f);
    CHECK(cam.yaw() <= FreeCam::kPi && cam.yaw() >= -FreeCam::kPi);
    CHECK(near(cam.yaw(), -FreeCam::kPi / 2));
    CHECK(cam.pitch() < FreeCam::kPi / 2 && cam.pitch() > 1.5f);
    cam.look(0, -10.0f);
    CHECK(cam.pitch() > -FreeCam::kPi / 2 && cam.pitch() < -1.5f);
  }
  {  // move: forward along the view, strafe right = screen right (cross(up, forward)), up is the world axis, speed scales
    FreeCam cam;
    cam.set_on(true);
    cam.init_from({0, 0, 0}, {0, 0, 1});
    cam.move(1, 0, 0, 1.0f);
    CHECK(near(cam.eye()[2], FreeCam::kBaseSpeed) && near(cam.eye()[0], 0));
    cam.init_from({0, 0, 0}, {0, 0, 1});
    cam.move(0, 1, 0, 1.0f);
    // Looking down +z, right = +x.
    CHECK(near(cam.eye()[0], FreeCam::kBaseSpeed) && near(cam.eye()[2], 0));
    // Looking down +x: right = -z.
    cam.init_from({0, 0, 0}, {1, 0, 0});
    cam.move(0, 1, 0, 1.0f);
    CHECK(near(cam.eye()[2], -FreeCam::kBaseSpeed) && near(cam.eye()[0], 0));
    // Pitched 45 degrees up: forward climbs.
    cam.init_from({0, 0, 0}, {0, 1, 1});
    cam.move(1, 0, 0, 1.0f);
    CHECK(cam.eye()[1] > 100 && near(cam.eye()[1], cam.eye()[2]));
    cam.init_from({0, 0, 0}, {0, 0, 1});
    cam.set_speed(2.0f);
    cam.move(0, 0, 1, 0.5f);
    CHECK(near(cam.eye()[1], FreeCam::kBaseSpeed));
    cam.init_from({0, 0, 0}, {0, 0, 1});
    cam.set_speed(1.0f);
    // Sprint.
    cam.move(1, 0, 0, 1.0f, true);
    CHECK(near(cam.eye()[2], FreeCam::kBaseSpeed * FreeCam::kSprint, 0.5f));
    // An arena in ~2 s.
    CHECK(FreeCam::kBaseSpeed * 2.0f >= 8000.0f);
    cam.set_speed(100.0f);
    CHECK(cam.speed() <= 20.0f);
  }
  {  // gating: hide only counts while on; reset re-seeds; switching on again re-seeds
    FreeCam cam;
    cam.set_hide_overlays(true);
    CHECK(!cam.hide_overlays());
    cam.set_on(true);
    CHECK(cam.hide_overlays());
    cam.init_from({0, 0, 0}, {0, 0, 1});
    cam.reset();
    CHECK(cam.needs_init());
    cam.init_from({0, 0, 0}, {0, 0, 1});
    cam.set_on(false);
    CHECK(!cam.hide_overlays() && !cam.needs_init());
    cam.set_on(true);
    CHECK(cam.needs_init());
  }
  if (failures) {
    std::printf("%d failure(s)\n", failures);
    return 1;
  }
  std::printf("free cam tests ok\n");
  return 0;
}
