#include "core/dreamcast_memory.h"

#include <algorithm>
#include <utility>

namespace jojo {
namespace {

Result<std::size_t> main_ram_offset(std::uint32_t address,
                                    std::size_t width,
                                    std::uint32_t alignment) {
    if (alignment > 1u && (address % alignment) != 0u) {
        return Result<std::size_t>::failure(ErrorCode::invalid_argument,
                                            "Dreamcast main RAM access is misaligned");
    }

    constexpr std::uint32_t bases[] = {
        kDreamcastMainRamPhysicalBase,
        kDreamcastMainRamCachedBase,
        kDreamcastMainRamUncachedBase,
    };
    for (const auto base : bases) {
        if (address < base) continue;
        const auto area_offset = static_cast<std::uint64_t>(address) - base;
        if (area_offset >= kDreamcastMainRamAreaSpan) continue;

        const auto ram_offset = static_cast<std::size_t>(
            area_offset % static_cast<std::uint64_t>(kDreamcastMainRamSize));
        if (width > kDreamcastMainRamSize - ram_offset) {
            return Result<std::size_t>::failure(
                ErrorCode::invalid_argument,
                "Dreamcast main RAM access crosses a 16 MiB mirror boundary");
        }
        return Result<std::size_t>::success(ram_offset);
    }

    return Result<std::size_t>::failure(ErrorCode::invalid_argument,
                                        "Dreamcast address is outside main RAM aliases");
}

}

Result<DreamcastExecutableMemory> load_dreamcast_boot_memory(
    const DreamcastBootProgram& program,
    std::uint32_t load_address) {
    if (program.bytes.empty()) {
        return Result<DreamcastExecutableMemory>::failure(
            ErrorCode::invalid_installation, "Dreamcast boot program is empty");
    }

    auto offset = main_ram_offset(load_address, program.bytes.size(), 1u);
    if (!offset) {
        return Result<DreamcastExecutableMemory>::failure(offset.error, offset.detail);
    }

    DreamcastExecutableMemory memory{};
    memory.main_ram.resize(kDreamcastMainRamSize, 0u);
    std::copy(program.bytes.begin(), program.bytes.end(),
              memory.main_ram.begin() + static_cast<std::ptrdiff_t>(offset.value));
    memory.load_address = load_address;
    memory.entry_pc = load_address;
    memory.program_size = program.bytes.size();
    return Result<DreamcastExecutableMemory>::success(std::move(memory));
}

Result<DreamcastPreparedExecutable> prepare_dreamcast_executable(
    const DreamcastBootProgram& program) {
    auto analysis = analyze_dreamcast_boot_program(program);
    if (!analysis) {
        return Result<DreamcastPreparedExecutable>::failure(analysis.error, analysis.detail);
    }

    auto memory = load_dreamcast_boot_memory(program, analysis.value.load_address);
    if (!memory) {
        return Result<DreamcastPreparedExecutable>::failure(memory.error, memory.detail);
    }

    DreamcastPreparedExecutable prepared{};
    prepared.analysis = std::move(analysis.value);
    prepared.memory = std::move(memory.value);
    return Result<DreamcastPreparedExecutable>::success(std::move(prepared));
}

Result<std::uint8_t> read_dreamcast_u8(const DreamcastExecutableMemory& memory,
                                       std::uint32_t address) {
    auto offset = main_ram_offset(address, 1u, 1u);
    if (!offset) return Result<std::uint8_t>::failure(offset.error, offset.detail);
    if (memory.main_ram.size() != kDreamcastMainRamSize) {
        return Result<std::uint8_t>::failure(ErrorCode::invalid_installation,
                                             "Dreamcast main RAM backing storage has an invalid size");
    }
    return Result<std::uint8_t>::success(memory.main_ram[offset.value]);
}

Result<std::uint16_t> read_dreamcast_u16(const DreamcastExecutableMemory& memory,
                                         std::uint32_t address) {
    auto offset = main_ram_offset(address, 2u, 2u);
    if (!offset) return Result<std::uint16_t>::failure(offset.error, offset.detail);
    if (memory.main_ram.size() != kDreamcastMainRamSize) {
        return Result<std::uint16_t>::failure(ErrorCode::invalid_installation,
                                              "Dreamcast main RAM backing storage has an invalid size");
    }
    const auto low = static_cast<std::uint16_t>(memory.main_ram[offset.value]);
    const auto high = static_cast<std::uint16_t>(memory.main_ram[offset.value + 1u]);
    const auto value = static_cast<std::uint16_t>(
        low | static_cast<std::uint16_t>(high << 8u));
    return Result<std::uint16_t>::success(value);
}

Result<std::uint32_t> read_dreamcast_u32(const DreamcastExecutableMemory& memory,
                                         std::uint32_t address) {
    auto offset = main_ram_offset(address, 4u, 4u);
    if (!offset) return Result<std::uint32_t>::failure(offset.error, offset.detail);
    if (memory.main_ram.size() != kDreamcastMainRamSize) {
        return Result<std::uint32_t>::failure(ErrorCode::invalid_installation,
                                              "Dreamcast main RAM backing storage has an invalid size");
    }
    const auto value = static_cast<std::uint32_t>(memory.main_ram[offset.value]) |
                       static_cast<std::uint32_t>(memory.main_ram[offset.value + 1u]) << 8u |
                       static_cast<std::uint32_t>(memory.main_ram[offset.value + 2u]) << 16u |
                       static_cast<std::uint32_t>(memory.main_ram[offset.value + 3u]) << 24u;
    return Result<std::uint32_t>::success(value);
}

Result<void> write_dreamcast_u8(DreamcastExecutableMemory& memory,
                                std::uint32_t address,
                                std::uint8_t value) {
    auto offset = main_ram_offset(address, 1u, 1u);
    if (!offset) return Result<void>::failure(offset.error, offset.detail);
    if (memory.main_ram.size() != kDreamcastMainRamSize) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "Dreamcast main RAM backing storage has an invalid size");
    }
    memory.main_ram[offset.value] = value;
    return Result<void>::success();
}

Result<void> write_dreamcast_u16(DreamcastExecutableMemory& memory,
                                 std::uint32_t address,
                                 std::uint16_t value) {
    auto offset = main_ram_offset(address, 2u, 2u);
    if (!offset) return Result<void>::failure(offset.error, offset.detail);
    if (memory.main_ram.size() != kDreamcastMainRamSize) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "Dreamcast main RAM backing storage has an invalid size");
    }
    memory.main_ram[offset.value] = static_cast<std::uint8_t>(value & 0xFFu);
    memory.main_ram[offset.value + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    return Result<void>::success();
}

Result<void> write_dreamcast_u32(DreamcastExecutableMemory& memory,
                                 std::uint32_t address,
                                 std::uint32_t value) {
    auto offset = main_ram_offset(address, 4u, 4u);
    if (!offset) return Result<void>::failure(offset.error, offset.detail);
    if (memory.main_ram.size() != kDreamcastMainRamSize) {
        return Result<void>::failure(ErrorCode::invalid_installation,
                                     "Dreamcast main RAM backing storage has an invalid size");
    }
    memory.main_ram[offset.value] = static_cast<std::uint8_t>(value & 0xFFu);
    memory.main_ram[offset.value + 1u] = static_cast<std::uint8_t>((value >> 8u) & 0xFFu);
    memory.main_ram[offset.value + 2u] = static_cast<std::uint8_t>((value >> 16u) & 0xFFu);
    memory.main_ram[offset.value + 3u] = static_cast<std::uint8_t>((value >> 24u) & 0xFFu);
    return Result<void>::success();
}

}
