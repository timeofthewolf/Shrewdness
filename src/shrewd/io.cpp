#include "shrewd/io.hpp"

#include <istream>
#include <limits>
#include <ostream>
#include <utility>

namespace shrewd {

BufferedIo::BufferedIo(std::vector<Value> input, std::size_t output_limit)
    : input_(std::move(input)),
      limit_(output_limit ? output_limit
                          : std::numeric_limits<std::size_t>::max()) {}

StreamIo::StreamIo(std::istream &in, std::ostream &out) : in_(in), out_(out) {}

Value StreamIo::read() {
    out_.flush();
    const int c = in_.get();
    ++reads_;
    return c == std::istream::traits_type::eof()
               ? kEndOfInput
               : static_cast<Value>(static_cast<unsigned char>(c));
}

void StreamIo::put(char c) {
    out_.put(c);
    if (c == '\n')
        out_.flush();
}

} // namespace shrewd
