#pragma once

namespace spyglass::overlay {

class View {
public:
    void draw();

    void toggle() noexcept { visible_ = !visible_; }
    [[nodiscard]] bool visible() const noexcept { return visible_; }

private:
    bool visible_{false};
};

}  // namespace spyglass::overlay
