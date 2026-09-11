#include "core/ps1_max3_coverage.h"

#include <algorithm>
#include <cstdint>
#include <type_traits>

namespace jojo {
namespace {

constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;

void mix_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

template <class T>
void mix_integral(std::uint64_t& hash, T value) noexcept {
    static_assert(std::is_integral_v<T> || std::is_enum_v<T>);
    using Raw = std::conditional_t<std::is_enum_v<T>, std::underlying_type_t<T>, T>;
    using Unsigned = std::make_unsigned_t<Raw>;
    const auto raw = static_cast<Unsigned>(value);
    for (std::size_t shift = 0u; shift < sizeof(Unsigned); ++shift) {
        mix_byte(hash, static_cast<std::uint8_t>((raw >> (shift * 8u)) & 0xffu));
    }
}

void mix_domain(std::uint64_t& hash, std::uint8_t domain) noexcept {
    mix_byte(hash, 0xf0u);
    mix_byte(hash, domain);
}

std::uint32_t opcode_family(std::uint32_t instruction) noexcept {
    const auto primary = (instruction >> 26u) & 0x3fu;
    if (primary == 0u) {
        return 0x100u | (instruction & 0x3fu);
    }
    if (primary == 0x10u || primary == 0x12u) {
        return (primary << 8u) | ((instruction >> 21u) & 0x1fu);
    }
    return primary;
}

template <class Set, class Mixer>
void mix_set(std::uint64_t& hash,
             std::uint8_t domain,
             const Set& values,
             Mixer mixer) noexcept {
    mix_domain(hash, domain);
    mix_integral(hash, static_cast<std::uint64_t>(values.size()));
    for (const auto& value : values) mixer(hash, value);
}

void observe_pc(std::set<std::uint32_t>& pcs,
                Ps1Max3CoverageDelta& delta,
                std::uint32_t pc) {
    if (pcs.insert(pc).second) ++delta.new_pcs;
}

void observe_opcode(std::set<std::uint32_t>& opcodes,
                    Ps1Max3CoverageDelta& delta,
                    std::uint32_t instruction) {
    if (opcodes.insert(opcode_family(instruction)).second) ++delta.new_opcodes;
}

void update_landmark(bool& seen,
                     std::uint64_t value,
                     std::uint64_t& maximum,
                     Ps1Max3CoverageDelta& delta) noexcept {
    maximum = std::max(maximum, value);
    if (!seen && value != 0u) {
        seen = true;
        ++delta.new_landmarks;
    }
}

} // namespace

Ps1Max3CoverageDelta Ps1Max3Coverage::observe(
    const Ps1BootReport& report,
    const Ps1Max3Frontier* frontier,
    std::optional<std::uint64_t> state_hash) {
    Ps1Max3CoverageDelta delta{};

    if (state_hash && states_.insert(*state_hash).second) {
        ++delta.new_states;
    }

    for (const auto& trace : report.recent_trace) {
        observe_pc(pcs_, delta, trace.pc);
        if (trace.opcode) observe_opcode(opcodes_, delta, *trace.opcode);
    }

    for (const auto& bios : report.recent_bios_calls) {
        observe_pc(pcs_, delta, bios.pc);
        if (bios_pairs_.insert(BiosKey{bios.table_physical, bios.selector}).second) {
            ++delta.new_bios_pairs;
        }
    }

    for (const auto& mmio : report.recent_mmio) {
        observe_pc(pcs_, delta, mmio.pc);
        if (mmio_tuples_.insert(MmioKey{mmio.address, mmio.width, mmio.write}).second) {
            ++delta.new_mmio_tuples;
        }
    }

    for (const auto& cdrom : report.recent_cdrom_commands) {
        if (cd_contexts_.insert(CdKey{cdrom.command, cdrom.index, cdrom.status}).second) {
            ++delta.new_cd_contexts;
        }
    }

    if (report.cpu_diagnostic) {
        const auto& diagnostic = *report.cpu_diagnostic;
        observe_pc(pcs_, delta, diagnostic.pc);
        if (diagnostic.opcode) observe_opcode(opcodes_, delta, *diagnostic.opcode);
        const bool has_exception = diagnostic.exception_code.has_value();
        const auto exception = has_exception
            ? static_cast<std::uint8_t>(*diagnostic.exception_code)
            : 0u;
        if (cpu_classes_.insert(CpuKey{
                static_cast<std::uint8_t>(diagnostic.boundary),
                static_cast<std::uint8_t>(diagnostic.stage),
                has_exception,
                exception,
            }).second) {
            ++delta.new_cpu_classes;
        }
    }

    if (frontier) {
        observe_pc(pcs_, delta, frontier->pc);
        if (frontier->opcode) observe_opcode(opcodes_, delta, *frontier->opcode);
        const bool has_opcode = frontier->opcode.has_value();
        const auto write_value = frontier->write ? frontier->value : 0u;
        const FrontierKey key{
            static_cast<std::uint8_t>(frontier->kind),
            frontier->pc,
            has_opcode,
            frontier->opcode.value_or(0u),
            frontier->table,
            frontier->selector,
            frontier->address,
            frontier->width,
            frontier->write,
            write_value,
        };
        if (frontiers_.insert(key).second) ++delta.new_frontiers;
    }

    update_landmark(saw_interrupt_, report.interrupts_accepted,
                    landmarks_.max_interrupts_accepted, delta);
    update_landmark(saw_cdrom_, report.cdrom_command_count,
                    landmarks_.max_cdrom_command_count, delta);
    update_landmark(saw_dma_, report.dma_transfer_count,
                    landmarks_.max_dma_transfer_count, delta);
    update_landmark(saw_gp0_, report.gpu_gp0_command_count,
                    landmarks_.max_gpu_gp0_command_count, delta);
    update_landmark(saw_gp1_, report.gpu_gp1_command_count,
                    landmarks_.max_gpu_gp1_command_count, delta);
    update_landmark(saw_vram_, report.vram_write_count,
                    landmarks_.max_vram_write_count, delta);
    update_landmark(saw_frame_, report.presented_frames,
                    landmarks_.max_presented_frames, delta);

    return delta;
}

const Ps1Max3ProgressLandmarks& Ps1Max3Coverage::landmarks() const noexcept {
    return landmarks_;
}

std::uint64_t Ps1Max3Coverage::deterministic_hash() const noexcept {
    std::uint64_t hash = kFnvOffset;

    mix_set(hash, 1u, states_, [](std::uint64_t& out, std::uint64_t value) {
        mix_integral(out, value);
    });
    mix_set(hash, 2u, pcs_, [](std::uint64_t& out, std::uint32_t value) {
        mix_integral(out, value);
    });
    mix_set(hash, 3u, opcodes_, [](std::uint64_t& out, std::uint32_t value) {
        mix_integral(out, value);
    });
    mix_set(hash, 4u, bios_pairs_, [](std::uint64_t& out, const BiosKey& value) {
        mix_integral(out, std::get<0>(value));
        mix_integral(out, std::get<1>(value));
    });
    mix_set(hash, 5u, mmio_tuples_, [](std::uint64_t& out, const MmioKey& value) {
        mix_integral(out, std::get<0>(value));
        mix_integral(out, std::get<1>(value));
        mix_integral(out, std::get<2>(value));
    });
    mix_set(hash, 6u, cd_contexts_, [](std::uint64_t& out, const CdKey& value) {
        mix_integral(out, std::get<0>(value));
        mix_integral(out, std::get<1>(value));
        mix_integral(out, std::get<2>(value));
    });
    mix_set(hash, 7u, cpu_classes_, [](std::uint64_t& out, const CpuKey& value) {
        mix_integral(out, std::get<0>(value));
        mix_integral(out, std::get<1>(value));
        mix_integral(out, std::get<2>(value));
        mix_integral(out, std::get<3>(value));
    });
    mix_set(hash, 8u, frontiers_, [](std::uint64_t& out, const FrontierKey& value) {
        std::apply([&out](const auto&... fields) {
            (mix_integral(out, fields), ...);
        }, value);
    });

    mix_domain(hash, 9u);
    mix_integral(hash, saw_interrupt_);
    mix_integral(hash, saw_cdrom_);
    mix_integral(hash, saw_dma_);
    mix_integral(hash, saw_gp0_);
    mix_integral(hash, saw_gp1_);
    mix_integral(hash, saw_vram_);
    mix_integral(hash, saw_frame_);
    mix_integral(hash, landmarks_.max_interrupts_accepted);
    mix_integral(hash, landmarks_.max_cdrom_command_count);
    mix_integral(hash, landmarks_.max_dma_transfer_count);
    mix_integral(hash, landmarks_.max_gpu_gp0_command_count);
    mix_integral(hash, landmarks_.max_gpu_gp1_command_count);
    mix_integral(hash, landmarks_.max_vram_write_count);
    mix_integral(hash, landmarks_.max_presented_frames);

    return hash;
}

} // namespace jojo
