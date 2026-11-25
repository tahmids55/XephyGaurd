#pragma once
#include "capture.h"

namespace xg {
// Cuts monitor rectangle `r` (pixels of the whole-desktop picture) out of `f` in place: the pixels become the
// monitor's, dirty rectangles are clipped to it and moved to its origin, width/height/stride follow, full_dirty is
// kept. `r` is clamped to the frame first. Returns false (f untouched) when nothing of `r` lies inside the frame,
// and true with an empty dirty list and full_dirty == false when nothing changed inside the monitor.
bool crop_frame(Frame& f, const MonitorInfo& r);
}
