#pragma once

#include <cstdint>
#include <string_view>

namespace shrewd {

using Gene = std::int32_t;

using Value = std::int64_t;

inline constexpr int kRegisterCount = 4;

#define SHREWD_OP_LIST(X)                                                      \
    X(NOP)                                                                     \
                                                                               \
    X(PUSHI)                                                                   \
    X(PUSH0)                                                                   \
    X(PUSH1)                                                                   \
    X(PUSH2)                                                                   \
    X(PUSH10)                                                                  \
    X(PUSHN1)                                                                  \
                                                                               \
    X(POP)                                                                     \
    X(DUP)                                                                     \
    X(SWAP)                                                                    \
    X(OVER)                                                                    \
                                                                               \
    X(ADD)                                                                     \
    X(SUB)                                                                     \
    X(MUL)                                                                     \
    X(DIV)                                                                     \
    X(MOD)                                                                     \
    X(NEG)                                                                     \
    X(INC)                                                                     \
    X(DEC)                                                                     \
                                                                               \
    X(LT)                                                                      \
    X(GT)                                                                      \
    X(EQ)                                                                      \
    X(NEQ)                                                                     \
    X(NOT)                                                                     \
    X(AND)                                                                     \
    X(OR)                                                                      \
                                                                               \
    X(LD0)                                                                     \
    X(LD1)                                                                     \
    X(LD2)                                                                     \
    X(LD3)                                                                     \
    X(ST0)                                                                     \
    X(ST1)                                                                     \
    X(ST2)                                                                     \
    X(ST3)                                                                     \
                                                                               \
    X(MLOAD)                                                                   \
    X(MSTORE)                                                                  \
    X(MSIZE)                                                                   \
                                                                               \
    X(IF)                                                                      \
    X(ELSE)                                                                    \
    X(DO)                                                                      \
    X(END)                                                                     \
    X(BREAK)                                                                   \
                                                                               \
    X(PROC)                                                                    \
    X(CALL)                                                                    \
    X(RET)                                                                     \
                                                                               \
    X(OUT)                                                                     \
    X(OUTNUM)                                                                  \
    X(IN)                                                                      \
    X(RAND)                                                                    \
                                                                               \
    X(GLEN)                                                                    \
    X(GREAD)                                                                   \
    X(EMIT)                                                                    \
    X(CLEN)                                                                    \
    X(CREAD)                                                                   \
    X(CWRITE)                                                                  \
    X(SPAWN)

enum class Op : std::uint8_t {
#define SHREWD_OP_ENUM(name) name,
    SHREWD_OP_LIST(SHREWD_OP_ENUM)
#undef SHREWD_OP_ENUM
        COUNT
};

inline constexpr int kOpCount = static_cast<int>(Op::COUNT);

constexpr int op_size(Op op) noexcept { return op == Op::PUSHI ? 2 : 1; }

struct OpInfo {
    std::string_view mnemonic;
    int size;
    std::string_view doc;
};

const OpInfo &info(Op op);

inline Op decode(Gene g) noexcept {
    int i = static_cast<int>(g % kOpCount);
    if (i < 0)
        i += kOpCount;
    return static_cast<Op>(i);
}

} // namespace shrewd
