#include <shader/usse_program_analyzer.h>

namespace {
using namespace shader::usse;

// Synthetic arithmetic markers and BR encodings; no game shader is embedded.
constexpr uint64_t cfg_marker = 0x4000000000000001ULL;
constexpr uint64_t cfg_branch(uint8_t predicate, uint32_t displacement) {
    return 0xf800004000000000ULL | (uint64_t(predicate) << 56) | displacement;
}

bool cfg_predicate(uint8_t predicate, uint8_t values) {
    if (!predicate)
        return true;
    if (predicate <= 4)
        return (values & (1 << (predicate - 1))) != 0;
    if (predicate <= 7)
        return (values & (1 << (predicate - 5))) == 0;
    return (values & 4) != 0;
}

std::vector<uint32_t> cfg_reference_trace(const std::vector<uint64_t> &instructions, uint8_t values) {
    std::vector<uint32_t> trace;
    for (uint32_t pc = 0; pc < instructions.size();) {
        uint8_t predicate = 0;
        int32_t displacement = 0;
        if (is_branch(instructions[pc], predicate, displacement)) {
            pc += cfg_predicate(predicate, values) ? displacement : 1;
        } else {
            if (cfg_predicate(get_predicate(instructions[pc]), values))
                trace.push_back(pc);
            ++pc;
        }
    }
    return trace;
}

void cfg_tree_trace(const USSEBaseNode &node, const std::vector<uint64_t> &instructions,
    uint8_t values, std::vector<uint32_t> &trace) {
    if (node.node_type() == USSE_CODE_NODE) {
        const auto &code = static_cast<const USSECodeNode &>(node);
        if (cfg_predicate(code.condition, values)) {
            for (uint32_t pc = code.offset; pc < code.offset + code.size; ++pc) {
                ASSERT_LT(pc, instructions.size());
                uint8_t predicate;
                int32_t displacement;
                // The real BR translator asserts: branches must be consumed by the CFG.
                ASSERT_FALSE(is_branch(instructions[pc], predicate, displacement));
                trace.push_back(pc);
            }
        }
    } else if (node.node_type() == USSE_CONDITIONAL_NODE) {
        const auto &conditional = static_cast<const USSEConditionalNode &>(node);
        const auto *block = cfg_predicate(conditional.negif_condition(), values)
            ? conditional.else_block() : conditional.if_block();
        if (block)
            cfg_tree_trace(*block, instructions, values, trace);
    } else {
        ASSERT_EQ(node.node_type(), USSE_BLOCK_NODE);
        for (size_t i = 0; i < node.children_count(); ++i)
            cfg_tree_trace(*node.children_at(i), instructions, values, trace);
    }
}

void cfg_expect_equivalent(const std::vector<uint64_t> &instructions) {
    USSEBlockNode root(nullptr, 0);
    analyze(root, static_cast<uint32_t>(instructions.size() - 1),
        [&](uint32_t pc) { return instructions.at(pc); });
    for (uint8_t values = 0; values < 16; ++values) {
        std::vector<uint32_t> actual;
        cfg_tree_trace(root, instructions, values, actual);
        EXPECT_EQ(actual, cfg_reference_trace(instructions, values)) << "predicate bits " << unsigned(values);
    }
}
}

TEST(USSEControlFlow, PredicatedFallthroughBeforeIfElsePreservesBothPaths) {
    // Minimal CFG from the crash: !P0 BR +1 immediately before an if/else.
    cfg_expect_equivalent({cfg_marker, cfg_branch(5, 1), cfg_branch(1, 3),
        cfg_marker, cfg_branch(0, 2), cfg_marker, cfg_marker});
}

TEST(USSEControlFlow, EveryFallthroughPredicateConsumesTheBranch) {
    for (uint8_t predicate = 0; predicate < 8; ++predicate)
        cfg_expect_equivalent({cfg_marker, cfg_branch(predicate, 1), cfg_marker});
}

TEST(USSEControlFlow, FallthroughAtProgramEndDoesNotReadBeyondProgram) {
    cfg_expect_equivalent({cfg_marker, cfg_branch(5, 1)});
}

TEST(USSEControlFlow, OrdinaryNestedConditionalBranchesKeepTheirPaths) {
    cfg_expect_equivalent({cfg_branch(1, 6), cfg_marker, cfg_branch(2, 3),
        cfg_marker, cfg_branch(0, 2), cfg_marker, cfg_marker});
}
