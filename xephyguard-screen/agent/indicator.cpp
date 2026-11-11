#include "indicator.h"

namespace xg {

void ScreenIndicator::set_visible(bool visible) {
  visible_ = visible;
}

bool ScreenIndicator::is_visible() const {
  return visible_;
}

}  // namespace xg
