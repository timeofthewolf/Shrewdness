#pragma once

#include "shrewd/isa.hpp"

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace shrewd {

inline constexpr Value kEndOfInput = -1;

class Io {
  public:
    virtual ~Io() = default;

    virtual Value read() = 0;

    virtual void put(char c) = 0;
};

class BufferedIo final : public Io {
  public:
    BufferedIo(std::vector<Value> input, std::size_t output_limit);

    Value read() override {
        return pos_ < input_.size() ? input_[pos_++] : kEndOfInput;
    }
    void put(char c) override {
        if (output_.size() < limit_)
            output_.push_back(c);
    }

    std::string &output() { return output_; }
    const std::string &output() const { return output_; }
    std::size_t inputs_read() const { return pos_; }

  private:
    std::vector<Value> input_;
    std::string output_;
    std::size_t pos_ = 0;
    std::size_t limit_;
};

class StreamIo final : public Io {
  public:
    StreamIo(std::istream &in, std::ostream &out);

    Value read() override;
    void put(char c) override;

    std::size_t inputs_read() const { return reads_; }

  private:
    std::istream &in_;
    std::ostream &out_;
    std::size_t reads_ = 0;
};

} // namespace shrewd
