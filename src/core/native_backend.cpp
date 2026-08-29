#include "core/native_backend.h"

#include "core/dreamcast_analysis.h"
#include "core/dreamcast_bus.h"
#include "core/sh4_cfg.h"
#include "core/version.h"

#include <array>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <utility>

namespace jojo {
namespace {

constexpr std::uint32_t kNativeBackendAbiVersion = 1u;
constexpr std::uint64_t kFnvOffset = 14695981039346656037ull;
constexpr std::uint64_t kFnvPrime = 1099511628211ull;
constexpr std::array<char, 8> kPlanMagic{{'J', 'O', 'J', 'O', 'N', 'B', '1', '\0'}};
constexpr std::uint32_t kMaxSerializedBlocks = 1u << 20u;
constexpr std::uint32_t kMaxSerializedOps = 1u << 22u;

void hash_byte(std::uint64_t& hash, std::uint8_t value) noexcept {
    hash ^= value;
    hash *= kFnvPrime;
}

void hash_u32(std::uint64_t& hash, std::uint32_t value) noexcept {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

void hash_u64(std::uint64_t& hash, std::uint64_t value) noexcept {
    for (unsigned shift = 0; shift < 64u; shift += 8u) {
        hash_byte(hash, static_cast<std::uint8_t>((value >> shift) & 0xFFu));
    }
}

std::string program_hash(const DreamcastBootProgram& program) {
    std::uint64_t hash = kFnvOffset;
    hash_u64(hash, static_cast<std::uint64_t>(program.bytes.size()));
    for (const auto byte : program.bytes) hash_byte(hash, byte);
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << hash;
    return out.str();
}

std::uint64_t runtime_hash(const NativeRuntime& runtime) noexcept {
    std::uint64_t hash = kFnvOffset;
    hash_u64(hash, runtime.frame_index);
    for (const auto value : runtime.cpu.r) hash_u32(hash, value);
    for (const auto value : runtime.cpu.r_bank) hash_u32(hash, value);
    for (const auto value : runtime.cpu.fr) hash_u32(hash, value);
    for (const auto value : runtime.cpu.xf) hash_u32(hash, value);
    hash_u32(hash, runtime.cpu.fpul);
    hash_u32(hash, runtime.cpu.fpscr);
    hash_u32(hash, runtime.cpu.pc);
    hash_u32(hash, runtime.cpu.pr);
    hash_u32(hash, runtime.cpu.gbr);
    hash_u32(hash, runtime.cpu.mach);
    hash_u32(hash, runtime.cpu.macl);
    hash_u32(hash, runtime.cpu.sr);
    hash_u32(hash, runtime.cpu.ssr);
    hash_u32(hash, runtime.cpu.spc);
    hash_u32(hash, runtime.cpu.sgr);
    hash_u32(hash, runtime.cpu.vbr);
    hash_u32(hash, runtime.cpu.dbr);
    hash_u32(hash, runtime.cpu.tra);
    hash_u32(hash, runtime.cpu.expevt);
    hash_u32(hash, runtime.cpu.intevt);
    hash_byte(hash, runtime.cpu.t ? 1u : 0u);
    hash_byte(hash, runtime.cpu.sleeping ? 1u : 0u);
    hash_u32(hash, static_cast<std::uint32_t>(runtime.cpu.last_system_event));
    hash_u32(hash, runtime.cpu.system_event_address);
    for (const auto byte : runtime.memory.main_ram) hash_byte(hash, byte);
    return hash == 0u ? 1u : hash;
}

std::optional<NativeHostOp> lower_host_op(Sh4IrOp op) noexcept {
    switch (op) {
        case Sh4IrOp::nop: return NativeHostOp::nop;
        case Sh4IrOp::clear_t: return NativeHostOp::clear_t;
        case Sh4IrOp::set_t: return NativeHostOp::set_t;
        case Sh4IrOp::move_t: return NativeHostOp::move_t;
        case Sh4IrOp::set_imm: return NativeHostOp::set_imm;
        case Sh4IrOp::add_imm: return NativeHostOp::add_imm;
        case Sh4IrOp::copy_reg: return NativeHostOp::copy_reg;
        case Sh4IrOp::add_reg: return NativeHostOp::add_reg;
        default: return std::nullopt;
    }
}

NativeBackend lower_backend(Sh4IrProgram ir, std::string hash) {
    NativeBackend backend{};
    backend.abi_version = kNativeBackendAbiVersion;
    backend.program_hash = std::move(hash);
    backend.ir = std::move(ir);
    backend.blocks.reserve(backend.ir.blocks.size());

    for (const auto& block : backend.ir.blocks) {
        NativeCompiledBlock compiled{};
        compiled.start_address = block.start_address;
        compiled.exit = block.exit;
        compiled.branch_target = block.branch_target;
        compiled.fallthrough_target = block.fallthrough_target;
        compiled.uses_native_lowering =
            block.exit == Sh4IrExit::end_of_stream || block.exit == Sh4IrExit::fallthrough;

        if (compiled.uses_native_lowering) {
            compiled.ops.reserve(block.ops.size());
            for (const auto& instruction : block.ops) {
                const auto lowered = lower_host_op(instruction.op);
                if (!lowered.has_value()) {
                    compiled.uses_native_lowering = false;
                    compiled.ops.clear();
                    break;
                }
                compiled.ops.push_back(NativeCompiledOp{
                    *lowered,
                    instruction.dst_reg,
                    instruction.src_reg,
                    instruction.imm,
                });
            }
        }

        if (compiled.uses_native_lowering) ++backend.native_block_count;
        else ++backend.fallback_block_count;
        backend.blocks.push_back(std::move(compiled));
    }
    return backend;
}

Result<NativeBackend> compile_backend(const DreamcastBootProgram& program,
                                      std::uint32_t load_address) {
    auto cfg = build_sh4_cfg(program.bytes, load_address, load_address);
    if (!cfg) return Result<NativeBackend>::failure(cfg.error, cfg.detail);
    if (!cfg.value.unsupported_sites.empty()) {
        std::ostringstream detail;
        detail << "native backend cannot compile unsupported SH-4 opcode at 0x"
               << std::hex << cfg.value.unsupported_sites.front();
        return Result<NativeBackend>::failure(ErrorCode::backend_unavailable, detail.str());
    }
    auto ir = lift_sh4_cfg(cfg.value);
    if (!ir) return Result<NativeBackend>::failure(ir.error, ir.detail);
    return Result<NativeBackend>::success(
        lower_backend(std::move(ir.value), program_hash(program)));
}

const NativeCompiledBlock* find_native_block(const NativeBackend& backend,
                                             std::uint32_t address) noexcept {
    for (const auto& block : backend.blocks) {
        if (block.start_address == address) return &block;
    }
    return nullptr;
}

Result<void> require_register(std::uint8_t reg) {
    if (reg >= 16u) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "native backend IR register is out of range");
    }
    return Result<void>::success();
}

Result<void> execute_native_op(const NativeCompiledOp& op, Sh4ReferenceState& state) {
    switch (op.op) {
        case NativeHostOp::nop:
            return Result<void>::success();
        case NativeHostOp::clear_t:
            state.t = false;
            return Result<void>::success();
        case NativeHostOp::set_t:
            state.t = true;
            return Result<void>::success();
        case NativeHostOp::move_t: {
            auto valid = require_register(op.dst_reg);
            if (!valid) return valid;
            state.r[op.dst_reg] = state.t ? 1u : 0u;
            return Result<void>::success();
        }
        case NativeHostOp::set_imm: {
            auto valid = require_register(op.dst_reg);
            if (!valid) return valid;
            state.r[op.dst_reg] = static_cast<std::uint32_t>(op.imm);
            return Result<void>::success();
        }
        case NativeHostOp::add_imm: {
            auto valid = require_register(op.dst_reg);
            if (!valid) return valid;
            state.r[op.dst_reg] += static_cast<std::uint32_t>(op.imm);
            return Result<void>::success();
        }
        case NativeHostOp::copy_reg: {
            auto dst = require_register(op.dst_reg);
            if (!dst) return dst;
            auto src = require_register(op.src_reg);
            if (!src) return src;
            state.r[op.dst_reg] = state.r[op.src_reg];
            return Result<void>::success();
        }
        case NativeHostOp::add_reg: {
            auto dst = require_register(op.dst_reg);
            if (!dst) return dst;
            auto src = require_register(op.src_reg);
            if (!src) return src;
            state.r[op.dst_reg] += state.r[op.src_reg];
            return Result<void>::success();
        }
    }
    return Result<void>::failure(ErrorCode::unsupported_format,
                                 "native backend encountered an unknown lowered operation");
}

std::size_t count_operations(const Sh4IrProgram& program) noexcept {
    std::size_t count{};
    for (const auto& block : program.blocks) count += block.ops.size();
    return count;
}

bool write_u8(std::ostream& out, std::uint8_t value) {
    out.put(static_cast<char>(value));
    return static_cast<bool>(out);
}

bool write_u32(std::ostream& out, std::uint32_t value) {
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        out.put(static_cast<char>((value >> shift) & 0xFFu));
    }
    return static_cast<bool>(out);
}

bool write_string(std::ostream& out, std::string_view value) {
    if (value.size() > std::numeric_limits<std::uint32_t>::max()) return false;
    if (!write_u32(out, static_cast<std::uint32_t>(value.size()))) return false;
    out.write(value.data(), static_cast<std::streamsize>(value.size()));
    return static_cast<bool>(out);
}

bool read_u8(std::istream& in, std::uint8_t& value) {
    const auto c = in.get();
    if (c == std::char_traits<char>::eof()) return false;
    value = static_cast<std::uint8_t>(c);
    return true;
}

bool read_u32(std::istream& in, std::uint32_t& value) {
    value = 0u;
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        std::uint8_t byte{};
        if (!read_u8(in, byte)) return false;
        value |= static_cast<std::uint32_t>(byte) << shift;
    }
    return true;
}

bool read_string(std::istream& in, std::string& value) {
    std::uint32_t size{};
    if (!read_u32(in, size) || size > 4096u) return false;
    value.resize(size);
    if (size != 0u) in.read(value.data(), static_cast<std::streamsize>(size));
    return static_cast<bool>(in);
}

Result<void> write_plan(const std::filesystem::path& path, const NativeBackend& backend) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) return Result<void>::failure(ErrorCode::io_error, "failed to create native compiled plan");
    out.write(kPlanMagic.data(), static_cast<std::streamsize>(kPlanMagic.size()));
    if (!write_u32(out, backend.abi_version) ||
        !write_string(out, backend.program_hash) ||
        !write_u32(out, backend.ir.entry_address) ||
        backend.ir.blocks.size() > std::numeric_limits<std::uint32_t>::max() ||
        !write_u32(out, static_cast<std::uint32_t>(backend.ir.blocks.size()))) {
        return Result<void>::failure(ErrorCode::io_error, "failed while writing native plan header");
    }

    for (const auto& block : backend.ir.blocks) {
        if (block.ops.size() > std::numeric_limits<std::uint32_t>::max()) {
            return Result<void>::failure(ErrorCode::io_error, "native plan block is too large");
        }
        if (!write_u32(out, block.start_address) ||
            !write_u32(out, static_cast<std::uint32_t>(block.exit)) ||
            !write_u8(out, block.branch_target.has_value() ? 1u : 0u) ||
            (block.branch_target.has_value() && !write_u32(out, *block.branch_target)) ||
            !write_u8(out, block.fallthrough_target.has_value() ? 1u : 0u) ||
            (block.fallthrough_target.has_value() && !write_u32(out, *block.fallthrough_target)) ||
            !write_u32(out, static_cast<std::uint32_t>(block.ops.size()))) {
            return Result<void>::failure(ErrorCode::io_error, "failed while writing native plan block");
        }
        for (const auto& op : block.ops) {
            if (!write_u32(out, static_cast<std::uint32_t>(op.op)) ||
                !write_u32(out, op.source_address) ||
                !write_u8(out, op.dst_reg) ||
                !write_u8(out, op.src_reg) ||
                !write_u32(out, static_cast<std::uint32_t>(op.imm)) ||
                !write_u32(out, op.target) ||
                !write_u8(out, op.in_delay_slot ? 1u : 0u)) {
                return Result<void>::failure(ErrorCode::io_error, "failed while writing native plan operation");
            }
        }
    }
    out.flush();
    if (!out) return Result<void>::failure(ErrorCode::io_error, "failed to flush native compiled plan");
    return Result<void>::success();
}

struct CacheManifest {
    std::uint32_t abi_version{};
    std::string core_version;
    std::string program_hash;
    std::size_t block_count{};
    std::size_t operation_count{};
};

Result<std::uint64_t> parse_u64(std::string_view text) {
    std::uint64_t value{};
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return Result<std::uint64_t>::failure(ErrorCode::invalid_installation,
                                              "native cache manifest contains an invalid integer");
    }
    return Result<std::uint64_t>::success(value);
}

Result<CacheManifest> read_cache_manifest(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) return Result<CacheManifest>::failure(ErrorCode::file_not_found, "native cache manifest is missing");
    CacheManifest manifest{};
    bool have_abi = false;
    bool have_core = false;
    bool have_hash = false;
    bool have_blocks = false;
    bool have_ops = false;
    std::string line;
    while (std::getline(in, line)) {
        const auto split = line.find('=');
        if (split == std::string::npos) continue;
        const auto key = std::string_view(line).substr(0, split);
        const auto value = std::string_view(line).substr(split + 1u);
        if (key == "abi_version") {
            auto parsed = parse_u64(value);
            if (!parsed || parsed.value > std::numeric_limits<std::uint32_t>::max())
                return Result<CacheManifest>::failure(ErrorCode::invalid_installation, "native cache ABI is invalid");
            manifest.abi_version = static_cast<std::uint32_t>(parsed.value);
            have_abi = true;
        } else if (key == "core_version") {
            manifest.core_version = std::string(value);
            have_core = true;
        } else if (key == "program_hash") {
            manifest.program_hash = std::string(value);
            have_hash = true;
        } else if (key == "block_count") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<CacheManifest>::failure(parsed.error, parsed.detail);
            manifest.block_count = static_cast<std::size_t>(parsed.value);
            have_blocks = true;
        } else if (key == "operation_count") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<CacheManifest>::failure(parsed.error, parsed.detail);
            manifest.operation_count = static_cast<std::size_t>(parsed.value);
            have_ops = true;
        }
    }
    if (!have_abi || !have_core || !have_hash || !have_blocks || !have_ops) {
        return Result<CacheManifest>::failure(ErrorCode::invalid_installation,
                                              "native cache manifest is incomplete");
    }
    return Result<CacheManifest>::success(std::move(manifest));
}

Result<void> write_cache_manifest(const std::filesystem::path& path,
                                  const NativeBackend& backend) {
    std::ofstream out(path, std::ios::trunc);
    if (!out) return Result<void>::failure(ErrorCode::io_error, "failed to create native cache manifest");
    out << "abi_version=" << backend.abi_version << '\n';
    out << "core_version=" << core_version() << '\n';
    out << "program_hash=" << backend.program_hash << '\n';
    out << "block_count=" << backend.ir.blocks.size() << '\n';
    out << "operation_count=" << count_operations(backend.ir) << '\n';
    out.flush();
    if (!out) return Result<void>::failure(ErrorCode::io_error, "failed while writing native cache manifest");
    return Result<void>::success();
}

Result<void> replace_cache_file(const std::filesystem::path& temp,
                                const std::filesystem::path& target) {
    std::error_code ec;
    std::filesystem::remove(target, ec);
    ec.clear();
    std::filesystem::rename(temp, target, ec);
    if (ec) return Result<void>::failure(ErrorCode::io_error, "failed to replace native cache file: " + ec.message());
    return Result<void>::success();
}

NativeBackendCacheInfo cache_info(const NativeBackend& backend,
                                  bool rebuilt,
                                  const std::filesystem::path& manifest_path,
                                  const std::filesystem::path& plan_path) {
    return NativeBackendCacheInfo{
        rebuilt,
        backend.abi_version,
        backend.program_hash,
        backend.ir.blocks.size(),
        count_operations(backend.ir),
        manifest_path,
        plan_path,
    };
}

}

std::uint32_t native_backend_abi_version() noexcept {
    return kNativeBackendAbiVersion;
}

Result<NativeRuntime> create_native_runtime(const DreamcastBootProgram& program) {
    auto prepared = prepare_dreamcast_executable(program);
    if (!prepared) return Result<NativeRuntime>::failure(prepared.error, prepared.detail);
    auto backend = compile_backend(program, prepared.value.analysis.load_address);
    if (!backend) return Result<NativeRuntime>::failure(backend.error, backend.detail);

    NativeRuntime runtime{};
    runtime.backend = std::move(backend.value);
    runtime.memory = std::move(prepared.value.memory);
    runtime.cpu.pc = runtime.memory.entry_pc;
    runtime.state_hash = runtime_hash(runtime);
    return Result<NativeRuntime>::success(std::move(runtime));
}

Result<NativeFrameStep> step_native_frame(NativeRuntime& runtime,
                                          std::size_t max_blocks) {
    if (max_blocks == 0u) {
        return Result<NativeFrameStep>::failure(ErrorCode::invalid_argument,
                                                "native frame block limit must be non-zero");
    }
    NativeFrameStep step{};
    DreamcastReferenceBus bus(runtime.memory);

    while (step.blocks_executed < max_blocks) {
        const auto* compiled = find_native_block(runtime.backend, runtime.cpu.pc);
        if (compiled == nullptr) break;
        const auto* ir_block = find_sh4_ir_block(runtime.backend.ir, compiled->start_address);
        if (ir_block == nullptr) {
            return Result<NativeFrameStep>::failure(ErrorCode::invalid_installation,
                                                    "native compiled block has no matching IR block");
        }

        if (compiled->uses_native_lowering) {
            for (const auto& op : compiled->ops) {
                auto executed = execute_native_op(op, runtime.cpu);
                if (!executed) return Result<NativeFrameStep>::failure(executed.error, executed.detail);
                ++step.operations_executed;
            }
            ++step.blocks_executed;
            if (compiled->exit == Sh4IrExit::end_of_stream) {
                runtime.cpu.pc = ir_block->ops.empty()
                    ? ir_block->start_address
                    : ir_block->ops.back().source_address + 2u;
                step.reached_end = true;
                break;
            }
            if (!compiled->fallthrough_target.has_value()) {
                return Result<NativeFrameStep>::failure(ErrorCode::invalid_argument,
                                                        "native fallthrough block is missing its target");
            }
            runtime.cpu.pc = *compiled->fallthrough_target;
            continue;
        }

        step.used_reference_fallback = true;
        Sh4IrProgram one_block{};
        one_block.entry_address = ir_block->start_address;
        one_block.blocks.push_back(*ir_block);
        auto executed = execute_sh4_ir_reference(one_block, runtime.cpu, bus, 1u);
        if (!executed) return Result<NativeFrameStep>::failure(executed.error, executed.detail);
        step.blocks_executed += executed.value.blocks_executed;
        step.operations_executed += executed.value.operations_executed;
        if (runtime.cpu.sleeping) break;
        if (ir_block->exit == Sh4IrExit::end_of_stream) {
            step.reached_end = true;
            break;
        }
    }

    ++runtime.frame_index;
    runtime.state_hash = runtime_hash(runtime);
    return Result<NativeFrameStep>::success(step);
}

Result<NativeBackend> load_native_backend_cache(const std::filesystem::path& plan_path) {
    std::ifstream in(plan_path, std::ios::binary);
    if (!in) return Result<NativeBackend>::failure(ErrorCode::file_not_found, "native compiled plan is missing");

    std::array<char, 8> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kPlanMagic) {
        return Result<NativeBackend>::failure(ErrorCode::invalid_installation,
                                              "native compiled plan has an invalid header");
    }

    std::uint32_t abi{};
    std::string hash;
    std::uint32_t entry{};
    std::uint32_t block_count{};
    if (!read_u32(in, abi) || !read_string(in, hash) || !read_u32(in, entry) || !read_u32(in, block_count)) {
        return Result<NativeBackend>::failure(ErrorCode::invalid_installation,
                                              "native compiled plan header is truncated");
    }
    if (abi != kNativeBackendAbiVersion || block_count > kMaxSerializedBlocks) {
        return Result<NativeBackend>::failure(ErrorCode::invalid_installation,
                                              "native compiled plan ABI or block count is invalid");
    }

    Sh4IrProgram ir{};
    ir.entry_address = entry;
    ir.blocks.reserve(block_count);
    for (std::uint32_t block_index = 0; block_index < block_count; ++block_index) {
        Sh4IrBlock block{};
        std::uint32_t exit_raw{};
        std::uint8_t has_branch{};
        std::uint8_t has_fallthrough{};
        std::uint32_t op_count{};
        if (!read_u32(in, block.start_address) || !read_u32(in, exit_raw) ||
            exit_raw > static_cast<std::uint32_t>(Sh4IrExit::return_exception) ||
            !read_u8(in, has_branch) || has_branch > 1u) {
            return Result<NativeBackend>::failure(ErrorCode::invalid_installation,
                                                  "native compiled plan block header is invalid");
        }
        block.exit = static_cast<Sh4IrExit>(exit_raw);
        if (has_branch != 0u) {
            std::uint32_t target{};
            if (!read_u32(in, target)) return Result<NativeBackend>::failure(ErrorCode::invalid_installation, "native plan branch target is truncated");
            block.branch_target = target;
        }
        if (!read_u8(in, has_fallthrough) || has_fallthrough > 1u) {
            return Result<NativeBackend>::failure(ErrorCode::invalid_installation, "native plan fallthrough flag is invalid");
        }
        if (has_fallthrough != 0u) {
            std::uint32_t target{};
            if (!read_u32(in, target)) return Result<NativeBackend>::failure(ErrorCode::invalid_installation, "native plan fallthrough target is truncated");
            block.fallthrough_target = target;
        }
        if (!read_u32(in, op_count) || op_count > kMaxSerializedOps) {
            return Result<NativeBackend>::failure(ErrorCode::invalid_installation, "native plan operation count is invalid");
        }
        block.ops.reserve(op_count);
        for (std::uint32_t op_index = 0; op_index < op_count; ++op_index) {
            Sh4IrInstruction op{};
            std::uint32_t op_raw{};
            std::uint32_t imm_raw{};
            std::uint8_t delay{};
            if (!read_u32(in, op_raw) || op_raw > static_cast<std::uint32_t>(Sh4IrOp::load_pc_address) ||
                !read_u32(in, op.source_address) || !read_u8(in, op.dst_reg) ||
                !read_u8(in, op.src_reg) || !read_u32(in, imm_raw) ||
                !read_u32(in, op.target) || !read_u8(in, delay) || delay > 1u) {
                return Result<NativeBackend>::failure(ErrorCode::invalid_installation,
                                                      "native compiled plan operation is invalid");
            }
            op.op = static_cast<Sh4IrOp>(op_raw);
            op.imm = static_cast<std::int32_t>(imm_raw);
            op.in_delay_slot = delay != 0u;
            block.ops.push_back(op);
        }
        ir.blocks.push_back(std::move(block));
    }
    if (!in.good() && !in.eof()) {
        return Result<NativeBackend>::failure(ErrorCode::invalid_installation,
                                              "native compiled plan could not be read completely");
    }
    return Result<NativeBackend>::success(lower_backend(std::move(ir), std::move(hash)));
}

Result<NativeBackendCacheInfo> ensure_native_backend_cache(
    const DreamcastBootProgram& program,
    const std::filesystem::path& install_dir) {
    if (install_dir.empty()) {
        return Result<NativeBackendCacheInfo>::failure(ErrorCode::invalid_argument,
                                                       "native cache installation directory cannot be empty");
    }
    const auto cache_dir = install_dir / "cache" / "native";
    const auto manifest_path = cache_dir / "backend_cache.ini";
    const auto plan_path = cache_dir / "compiled_plan.bin";
    const auto expected_hash = program_hash(program);

    std::error_code ec;
    std::filesystem::create_directories(cache_dir, ec);
    if (ec) return Result<NativeBackendCacheInfo>::failure(ErrorCode::io_error, ec.message());

    const auto manifest = read_cache_manifest(manifest_path);
    if (manifest && manifest.value.abi_version == kNativeBackendAbiVersion &&
        manifest.value.core_version == core_version() &&
        manifest.value.program_hash == expected_hash) {
        auto loaded = load_native_backend_cache(plan_path);
        if (loaded && loaded.value.abi_version == kNativeBackendAbiVersion &&
            loaded.value.program_hash == expected_hash &&
            loaded.value.ir.blocks.size() == manifest.value.block_count &&
            count_operations(loaded.value.ir) == manifest.value.operation_count) {
            return Result<NativeBackendCacheInfo>::success(
                cache_info(loaded.value, false, manifest_path, plan_path));
        }
    }

    auto analysis = analyze_dreamcast_boot_program(program);
    if (!analysis) return Result<NativeBackendCacheInfo>::failure(analysis.error, analysis.detail);
    auto backend = compile_backend(program, analysis.value.load_address);
    if (!backend) return Result<NativeBackendCacheInfo>::failure(backend.error, backend.detail);

    auto plan_temp = plan_path;
    plan_temp += ".tmp";
    auto manifest_temp = manifest_path;
    manifest_temp += ".tmp";
    auto plan_written = write_plan(plan_temp, backend.value);
    if (!plan_written) return Result<NativeBackendCacheInfo>::failure(plan_written.error, plan_written.detail);
    auto manifest_written = write_cache_manifest(manifest_temp, backend.value);
    if (!manifest_written) return Result<NativeBackendCacheInfo>::failure(manifest_written.error, manifest_written.detail);
    auto plan_replaced = replace_cache_file(plan_temp, plan_path);
    if (!plan_replaced) return Result<NativeBackendCacheInfo>::failure(plan_replaced.error, plan_replaced.detail);
    auto manifest_replaced = replace_cache_file(manifest_temp, manifest_path);
    if (!manifest_replaced) return Result<NativeBackendCacheInfo>::failure(manifest_replaced.error, manifest_replaced.detail);

    return Result<NativeBackendCacheInfo>::success(
        cache_info(backend.value, true, manifest_path, plan_path));
}

}
