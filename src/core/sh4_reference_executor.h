#pragma once

#include "core/result.h"
#include "core/sh4_ir.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <string>

namespace jojo {

struct Sh4ReferenceState {
    std::array<std::uint32_t, 16> r{};
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
    std::uint32_t intevt{};
    bool t{};
};

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
    end_of_stream,
    left_program,
    block_limit,
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
