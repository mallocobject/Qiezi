#pragma once

#include "define.hpp"
#include "glm/fwd.hpp"
#include <cassert>
#include <cstddef>
#include <memory>

template <typename T>
class Buffer {
  public:
    Buffer(int w, int h) : w_(w), h_(h) {
        buffer_ = std::make_unique<T[]>((size_t)w * h);
    }

    ~Buffer() {
    }

    T &operator()(int x, int y) {
        assert(x >= 0 && x < w_ && y >= 0 && y < h_);
        return buffer_[y * w_ + x];
    }

    const T &operator()(int x, int y) const {
        assert(x >= 0 && x < w_ && y >= 0 && y < h_);
        return buffer_[y * w_ + x];
    }

    T &operator()(const glm::i32vec2 &point) {
        return this->operator()(point.x, point.y);
    }

    const T &operator()(const glm::i32vec2 &point) const {
        return this->operator()(point.x, point.y);
    }

    int w() const noexcept {
        return w_;
    }

    int h() const noexcept {
        return h_;
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return static_cast<std::size_t>(w_) * h_;
    }

    T *data() noexcept {
        return buffer_.get();
    }

    const T *data() const noexcept {
        return buffer_.get();
    }

  private:
    int w_{0};
    int h_{0};
    std::unique_ptr<T[]> buffer_;
};