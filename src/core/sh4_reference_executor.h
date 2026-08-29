#pragma once

#include "core/result.h"
#include "core/sh4_ir.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>

namespace jojo {

struct Sh4ReferenceState {
    std::array<std::uint32_t, 16> r{};
    std::array<std::uint32_t, 8> r_bank{};
    std::array<std::uint32_t, 16> fr{};
    std::array<std::uint32_t, 16> xf{};
    std::uint32_t fpul{};
    std::uint32_t fpscr{0x00040001u};
    std::uint32_t pc{};
    std::uint32_t pr{};
    std::uint32_t gbr{};
    std::uint32_t mach{};
    std::uint32_t macl{};
    std::uint32_t sr{};
    std::uint32_t ssr{};
    std::uint32_t spc{};
    std::uint32_t sgr{};
    std::uint32_t vbr{};
    std::uint32_t dbr{};
    std::uint32_t tra{};
    std::uint32_t expevt{};
    std::uint32_t intevt{};
    bool t{};
    bool sleeping{};
    Sh4ReferenceSystemEvent last_system_event{Sh4ReferenceSystemEvent::none};
    std::uint32_t system_event_address{};
};

inline std::uint32_t read_sh4_reference_sr(const Sh4ReferenceState& state) noexcept {
    return (state.sr & ~1u) | (state.t ? 1u : 0u);
}

inline void write_sh4_reference_sr(Sh4ReferenceState& state, std::uint32_t value) noexcept {
    constexpr std::uint32_t md = 0x40000000u;
    constexpr std::uint32_t rb = 0x20000000u;
    const bool old_bank_one = (state.sr & (md | rb)) == (md | rb);
    const bool new_bank_one = (value & (md | rb)) == (md | rb);
    if (old_bank_one != new_bank_one) {
        for (std::size_t index = 0; index < state.r_bank.size(); ++index) {
            std::swap(state.r[index], state.r_bank[index]);
        }
    }
    state.sr = value & ~1u;
    state.t = (value & 1u) != 0u;
}

using Sh4ReferenceBlockBoundaryHook = std::function<Result<void>(Sh4ReferenceState&)>;

class Sh4ReferenceBus {
public:
    virtual ~Sh4ReferenceBus() = default;
    [[nodiscard]] virtual Result<std::uint8_t> read8(std::uint32_t address) = 0;
    [[nodiscard]] virtual Result<void> write8(std::uint32_t address, std::uint8_t value) = 0;
};

struct Sh4ReferenceBusFailure {
    ErrorCode error{ErrorCode::invalid_argument};
    std::string detail;
};

class Sh4ReferenceBytes;

class Sh4ReferenceByteProxy {
public:
    Sh4ReferenceByteProxy(Sh4ReferenceBytes& owner, std::size_t index) noexcept
        : owner_(&owner), index_(index) {}

    operator std::uint8_t() const;
    Sh4ReferenceByteProxy& operator=(std::uint8_t value);

private:
    Sh4ReferenceBytes* owner_{};
    std::size_t index_{};
};

class Sh4ReferenceBytes {
public:
    Sh4ReferenceBytes() = default;
    explicit Sh4ReferenceBytes(std::span<std::uint8_t> flat) noexcept
        : flat_(flat), virtual_size_(flat.size()) {}
    explicit Sh4ReferenceBytes(Sh4ReferenceBus& bus) noexcept
        : bus_(&bus), virtual_size_(static_cast<std::size_t>(1ull << 32u)) {}

    [[nodiscard]] std::size_t size() const noexcept { return virtual_size_; }
    [[nodiscard]] Sh4ReferenceByteProxy operator[](std::size_t index) noexcept {
        return Sh4ReferenceByteProxy(*this, index);
    }

private:
    friend class Sh4ReferenceByteProxy;

    [[nodiscard]] std::uint8_t read(std::size_t index) const;
    void write(std::size_t index, std::uint8_t value);

    std::span<std::uint8_t> flat_{};
    Sh4ReferenceBus* bus_{};
    std::size_t virtual_size_{};
};

struct Sh4ReferenceMemoryView {
    std::uint32_t base_address{};
    Sh4ReferenceBytes bytes{};

    Sh4ReferenceMemoryView() = default;
    Sh4ReferenceMemoryView(std::uint32_t base, std::span<std::uint8_t> flat) noexcept
        : base_address(base), bytes(flat) {}

    template <typename Container>
    Sh4ReferenceMemoryView(std::uint32_t base, Container& flat) noexcept
        : base_address(base),
          bytes(std::span<std::uint8_t>(flat.data(), flat.size())) {}

    [[nodiscard]] static Sh4ReferenceMemoryView for_bus(Sh4ReferenceBus& bus) noexcept {
        Sh4ReferenceMemoryView view{};
        view.base_address = 0u;
        view.bytes = Sh4ReferenceBytes(bus);
        return view;
    }
};

enum class Sh4ReferenceStopReason {
    left_program,
    end_of_stream,
    block_limit,
    sleep,
};

enum class Sh4ReferenceSystemEvent {
    none,
    ldtlb,
    movca_l,
    ocbi,
    ocbp,
    ocbwb,
    pref,
    sleep,
};

struct Sh4ReferenceRunResult {
    Sh4ReferenceStopReason stop_reason{Sh4ReferenceStopReason::end_of_stream};
    std::size_t blocks_executed{};
    std::size_t operations_executed{};
};

[[nodiscard]] Result<bool> accept_sh4_irl_interrupt(
    Sh4ReferenceState& state,
    std::uint8_t level);

[[nodiscard]] Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceMemoryView memory,
    std::size_t max_blocks);

[[nodiscard]] Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceMemoryView memory,
    std::size_t max_blocks,
    const Sh4ReferenceBlockBoundaryHook& boundary_hook);

[[nodiscard]] Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceBus& bus,
    std::size_t max_blocks);

[[nodiscard]] Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceBus& bus,
    std::size_t max_blocks,
    const Sh4ReferenceBlockBoundaryHook& boundary_hook);

}
