#pragma once

namespace xg {

class ScreenIndicator {
public:
  ScreenIndicator() = default;
  ~ScreenIndicator() = default;

  void set_visible(bool visible);
  bool is_visible() const;

private:
  bool visible_ = false;
};

}  // namespace xg
