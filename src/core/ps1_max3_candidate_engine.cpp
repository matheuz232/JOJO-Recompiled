#include "core/ps1_max3_candidate_engine.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace jojo {
namespace {

constexpr std::size_t kConstraintWindow = 8u;

std::uint32_t width_mask(std::uint8_t width) noexcept {
    switch (width) {
        case 1u: return 0x000000ffu;
        case 2u: return 0x0000ffffu;
        case 4u: return 0xffffffffu;
        default: return 0u;
    }
}

std::uint32_t sign_bit(std::uint8_t width) noexcept {
    switch (width) {
        case 1u: return 0x00000080u;
        case 2u: return 0x00008000u;
        case 4u: return 0x80000000u;
        default: return 0u;
    }
}

std::uint8_t opcode(std::uint32_t instruction) noexcept {
    return static_cast<std::uint8_t>((instruction >> 26u) & 0x3fu);
}

std::uint8_t rs(std::uint32_t instruction) noexcept {
    return static_cast<std::uint8_t>((instruction >> 21u) & 0x1fu);
}

std::uint8_t rt(std::uint32_t instruction) noexcept {
    return static_cast<std::uint8_t>((instruction >> 16u) & 0x1fu);
}

std::uint16_t immediate(std::uint32_t instruction) noexcept {
    return static_cast<std::uint16_t>(instruction & 0xffffu);
}

class CandidateBuilder {
public:
    CandidateBuilder(std::uint32_t mask, std::size_t maximum)
        : mask_(mask), maximum_(maximum) {}

    void append(std::uint32_t value, Ps1Max3CandidateSource source) {
        if (candidates_.size() >= maximum_) return;
        value &= mask_;
        const auto existing = std::find_if(
            candidates_.begin(), candidates_.end(),
            [value](const Ps1Max3ReadCandidate& candidate) {
                return candidate.value == value;
            });
        if (existing != candidates_.end()) return;
        candidates_.push_back(Ps1Max3ReadCandidate{value, source});
    }

    [[nodiscard]] bool full() const noexcept {
        return candidates_.size() >= maximum_;
    }

    [[nodiscard]] std::vector<Ps1Max3ReadCandidate> finish() {
        return std::move(candidates_);
    }

private:
    std::uint32_t mask_{};
    std::size_t maximum_{};
    std::vector<Ps1Max3ReadCandidate> candidates_;
};

void append_threshold_classes(CandidateBuilder& builder,
                              std::uint16_t immediate_value,
                              std::uint32_t mask) {
    const auto signed_immediate = static_cast<std::int32_t>(
        static_cast<std::int16_t>(immediate_value));
    const auto threshold = static_cast<std::uint32_t>(signed_immediate) & mask;
    if (threshold > 0u) {
        builder.append(threshold - 1u, Ps1Max3CandidateSource::threshold_class);
    }
    builder.append(threshold, Ps1Max3CandidateSource::threshold_class);
    if (threshold < mask) {
        builder.append(threshold + 1u, Ps1Max3CandidateSource::threshold_class);
    }
}

void append_local_constraints(CandidateBuilder& builder,
                              const Ps1Max3CandidateContext& context,
                              std::uint32_t mask) {
    std::uint8_t tracked_register = rt(context.load_opcode);
    const auto limit = std::min(kConstraintWindow, context.following_opcodes.size());
    for (std::size_t index = 0u; index < limit && !builder.full(); ++index) {
        const auto instruction = context.following_opcodes[index];
        const auto op = opcode(instruction);
        const auto source = rs(instruction);
        const auto target = rt(instruction);

        if (op == 0x0cu && source == tracked_register) { // ANDI
            const auto mask_value = static_cast<std::uint32_t>(immediate(instruction)) & mask;
            if (mask_value != 0u) {
                builder.append(mask_value, Ps1Max3CandidateSource::mask_class);
            }
            tracked_register = target;
            continue;
        }

        if ((op == 0x04u || op == 0x05u) && // BEQ/BNE against zero
            ((source == tracked_register && target == 0u) ||
             (target == tracked_register && source == 0u))) {
            builder.append(0u, Ps1Max3CandidateSource::zero_test_class);
            builder.append(1u, Ps1Max3CandidateSource::zero_test_class);
            continue;
        }

        if (op == 0x0bu && source == tracked_register) { // SLTIU
            append_threshold_classes(builder, immediate(instruction), mask);
            continue;
        }

        // Conservative local dataflow: once an immediate instruction overwrites the
        // tracked register from an unrelated source, stop following that value.
        if ((op >= 0x08u && op <= 0x0fu) && target == tracked_register &&
            source != tracked_register) {
            break;
        }
    }
}

} // namespace

std::vector<Ps1Max3ReadCandidate>
generate_ps1_max3_read_candidates(const Ps1Max3CandidateContext& context) {
    const auto mask = width_mask(context.width);
    if (context.profile == Ps1Max3Profile::strict ||
        context.max_candidates == 0u || mask == 0u) {
        return {};
    }

    CandidateBuilder builder(mask, context.max_candidates);
    builder.append(0u, Ps1Max3CandidateSource::baseline_zero);
    builder.append(1u, Ps1Max3CandidateSource::baseline_one);
    builder.append(mask, Ps1Max3CandidateSource::baseline_all_ones);

    if (context.profile == Ps1Max3Profile::omega) {
        builder.append(sign_bit(context.width), Ps1Max3CandidateSource::sign_bit);
    }

    for (const auto observed : context.strict_observed_values) {
        builder.append(observed, Ps1Max3CandidateSource::strict_observed);
    }

    if (context.profile == Ps1Max3Profile::omega && !builder.full()) {
        append_local_constraints(builder, context, mask);
    }

    return builder.finish();
}

} // namespace jojo
