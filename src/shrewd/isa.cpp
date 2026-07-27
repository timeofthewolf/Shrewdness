#include "shrewd/isa.hpp"

#include <array>

namespace shrewd {
namespace {

constexpr std::array<std::string_view, kOpCount> kNames = {
#define SHREWD_OP_NAME(name) #name,
    SHREWD_OP_LIST(SHREWD_OP_NAME)
#undef SHREWD_OP_NAME
};

constexpr std::array<OpInfo, kOpCount> kTable = {{
    {"NOP", 1, "do nothing"},

    {"PUSHI", 2, "push the next gene as a literal"},
    {"PUSH0", 1, "push 0"},
    {"PUSH1", 1, "push 1"},
    {"PUSH2", 1, "push 2"},
    {"PUSH10", 1, "push 10"},
    {"PUSHN1", 1, "push -1"},

    {"POP", 1, "discard the top value"},
    {"DUP", 1, "duplicate the top value"},
    {"SWAP", 1, "exchange the top two values"},
    {"OVER", 1, "copy the second value to the top"},

    {"ADD", 1, "a + b"},
    {"SUB", 1, "a - b"},
    {"MUL", 1, "a * b"},
    {"DIV", 1, "a / b, zero if b == 0"},
    {"MOD", 1, "a % b, zero if b == 0"},
    {"NEG", 1, "-a"},
    {"INC", 1, "a + 1"},
    {"DEC", 1, "a - 1"},

    {"LT", 1, "a < b"},
    {"GT", 1, "a > b"},
    {"EQ", 1, "a == b"},
    {"NEQ", 1, "a != b"},
    {"NOT", 1, "!a"},
    {"AND", 1, "a && b"},
    {"OR", 1, "a || b"},

    {"LD0", 1, "push R0"},
    {"LD1", 1, "push R1"},
    {"LD2", 1, "push R2"},
    {"LD3", 1, "push R3"},
    {"ST0", 1, "pop into R0"},
    {"ST1", 1, "pop into R1"},
    {"ST2", 1, "pop into R2"},
    {"ST3", 1, "pop into R3"},

    {"MLOAD", 1, "push mem[pop()]"},
    {"MSTORE", 1, "val = pop(); idx = pop(); mem[idx] = val"},
    {"MSIZE", 1, "push how many cells of tape I have"},

    {"IF", 1, "pop a condition; skip the block when it is zero"},
    {"ELSE", 1, "begin the alternative branch"},
    {"DO", 1, "loop start marker"},
    {"END", 1,
     "close a block; for DO, pop a condition and loop while non-zero"},
    {"BREAK", 1, "leave the innermost enclosing loop"},
    {"PROC", 1, "declare a procedure; skipped when reached in normal flow"},
    {"CALL", 1, "enter the pop()-th PROC in the genome, modulo how many exist"},
    {"RET", 1, "return early; halt if not inside a call"},

    {"OUT", 1, "pop a value and emit it as a character"},
    {"OUTNUM", 1, "pop a value and emit it as a decimal number"},
    {"IN", 1, "push the next input value, or -1 at end of input"},
    {"RAND", 1, "push a pseudorandom value in [0, 2^31)"},

    {"GLEN", 1, "push the length of my own genome"},
    {"GREAD", 1, "push my own gene at index pop()"},
    {"EMIT", 1, "append pop() to the child genome"},
    {"CLEN", 1, "push the length of the child genome so far"},
    {"CREAD", 1, "push the child's gene at index pop()"},
    {"CWRITE", 1, "val = pop(); idx = pop(); child[idx] = val"},
    {"SPAWN", 1, "commit the child genome as offspring and start a new one"},
}};

constexpr bool table_matches_op_list() {
    for (int i = 0; i < kOpCount; ++i) {
        if (kTable[i].mnemonic != kNames[i])
            return false;
        if (kTable[i].size != op_size(static_cast<Op>(i)))
            return false;
        if (kTable[i].doc.empty())
            return false;
    }
    return true;
}
static_assert(table_matches_op_list(),
              "kTable disagrees with SHREWD_OP_LIST in isa.hpp");

} // namespace

const OpInfo &info(Op op) { return kTable[static_cast<std::size_t>(op)]; }

} // namespace shrewd
