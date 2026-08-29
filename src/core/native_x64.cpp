#include "core/native_x64.h"

#include <cstddef>
#include <cstring>
#include <limits>

#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__x86_64__) || defined(__amd64__)
#include <sys/mman.h>
#endif

namespace jojo {
namespace {

#if defined(_M_X64) || defined(__x86_64__) || defined(__amd64__)
constexpr bool kX64 = true;
#else
constexpr bool kX64 = false;
#endif

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__) || defined(__amd64__))
constexpr std::uint8_t kStateBaseRm = 1u; // RCX: Windows x64 first integer argument.
#elif defined(__x86_64__) || defined(__amd64__)
constexpr std::uint8_t kStateBaseRm = 7u; // RDI: SysV x64 first integer argument.
#else
constexpr std::uint8_t kStateBaseRm = 0u;
#endif

void emit_u32(std::vector<std::uint8_t>& code, std::uint32_t value) {
    code.push_back(static_cast<std::uint8_t>(value & 0xFFu));
    code.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xFFu));
    code.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xFFu));
    code.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xFFu));
}

std::uint8_t memory_modrm(std::uint8_t reg_field) noexcept {
    return static_cast<std::uint8_t>(0x80u | ((reg_field & 7u) << 3u) | kStateBaseRm);
}

std::uint32_t checked_offset(std::size_t offset) {
    return static_cast<std::uint32_t>(offset);
}

std::uint32_t r_offset(std::uint8_t reg) {
    return checked_offset(offsetof(Sh4ReferenceState, r) +
                          static_cast<std::size_t>(reg) * sizeof(std::uint32_t));
}

std::uint32_t t_offset() {
    return checked_offset(offsetof(Sh4ReferenceState, t));
}

void emit_store_imm32(std::vector<std::uint8_t>& code,
                      std::uint32_t offset,
                      std::uint32_t value) {
    code.push_back(0xC7u); // MOV r/m32, imm32
    code.push_back(memory_modrm(0u));
    emit_u32(code, offset);
    emit_u32(code, value);
}

void emit_add_imm32(std::vector<std::uint8_t>& code,
                    std::uint32_t offset,
                    std::uint32_t value) {
    code.push_back(0x81u); // ADD r/m32, imm32
    code.push_back(memory_modrm(0u));
    emit_u32(code, offset);
    emit_u32(code, value);
}

void emit_load_eax32(std::vector<std::uint8_t>& code, std::uint32_t offset) {
    code.push_back(0x8Bu); // MOV EAX, r/m32
    code.push_back(memory_modrm(0u));
    emit_u32(code, offset);
}

void emit_store_eax32(std::vector<std::uint8_t>& code, std::uint32_t offset) {
    code.push_back(0x89u); // MOV r/m32, EAX
    code.push_back(memory_modrm(0u));
    emit_u32(code, offset);
}

void emit_add_eax32(std::vector<std::uint8_t>& code, std::uint32_t offset) {
    code.push_back(0x01u); // ADD r/m32, EAX
    code.push_back(memory_modrm(0u));
    emit_u32(code, offset);
}

void emit_store_imm8(std::vector<std::uint8_t>& code,
                     std::uint32_t offset,
                     std::uint8_t value) {
    code.push_back(0xC6u); // MOV r/m8, imm8
    code.push_back(memory_modrm(0u));
    emit_u32(code, offset);
    code.push_back(value);
}

void emit_loadzx_eax8(std::vector<std::uint8_t>& code, std::uint32_t offset) {
    code.push_back(0x0Fu);
    code.push_back(0xB6u); // MOVZX EAX, r/m8
    code.push_back(memory_modrm(0u));
    emit_u32(code, offset);
}

Result<void> validate_reg(std::uint8_t reg) {
    if (reg >= 16u) {
        return Result<void>::failure(ErrorCode::invalid_argument,
                                     "x64 lowering received an SH-4 register outside R0-R15");
    }
    return Result<void>::success();
}

Result<void> emit_instruction(std::vector<std::uint8_t>& code,
                              const Sh4IrInstruction& instruction) {
    switch (instruction.op) {
        case Sh4IrOp::nop:
            return Result<void>::success();
        case Sh4IrOp::clear_t:
            emit_store_imm8(code, t_offset(), 0u);
            return Result<void>::success();
        case Sh4IrOp::set_t:
            emit_store_imm8(code, t_offset(), 1u);
            return Result<void>::success();
        case Sh4IrOp::move_t: {
            auto valid = validate_reg(instruction.dst_reg);
            if (!valid) return valid;
            emit_loadzx_eax8(code, t_offset());
            emit_store_eax32(code, r_offset(instruction.dst_reg));
            return Result<void>::success();
        }
        case Sh4IrOp::set_imm: {
            auto valid = validate_reg(instruction.dst_reg);
            if (!valid) return valid;
            emit_store_imm32(code,
                             r_offset(instruction.dst_reg),
                             static_cast<std::uint32_t>(instruction.imm));
            return Result<void>::success();
        }
        case Sh4IrOp::add_imm: {
            auto valid = validate_reg(instruction.dst_reg);
            if (!valid) return valid;
            emit_add_imm32(code,
                           r_offset(instruction.dst_reg),
                           static_cast<std::uint32_t>(instruction.imm));
            return Result<void>::success();
        }
        case Sh4IrOp::copy_reg: {
            auto dst = validate_reg(instruction.dst_reg);
            if (!dst) return dst;
            auto src = validate_reg(instruction.src_reg);
            if (!src) return src;
            emit_load_eax32(code, r_offset(instruction.src_reg));
            emit_store_eax32(code, r_offset(instruction.dst_reg));
            return Result<void>::success();
        }
        case Sh4IrOp::add_reg: {
            auto dst = validate_reg(instruction.dst_reg);
            if (!dst) return dst;
            auto src = validate_reg(instruction.src_reg);
            if (!src) return src;
            emit_load_eax32(code, r_offset(instruction.src_reg));
            emit_add_eax32(code, r_offset(instruction.dst_reg));
            return Result<void>::success();
        }
        default:
            return Result<void>::failure(ErrorCode::unsupported_format,
                                         "SH-4 IR operation has no x64 machine-code lowering yet");
    }
}

}

bool native_x64_supported() noexcept {
    return kX64;
}

Result<std::vector<std::uint8_t>> compile_native_x64_block(const Sh4IrBlock& block) {
    if (!kX64) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::backend_unavailable,
            "native x64 backend requires an x86-64 host");
    }
    if (block.exit != Sh4IrExit::end_of_stream && block.exit != Sh4IrExit::fallthrough) {
        return Result<std::vector<std::uint8_t>>::failure(
            ErrorCode::unsupported_format,
            "x64 lowering currently supports straight-line SH-4 IR blocks only");
    }

    static_assert(offsetof(Sh4ReferenceState, t) <= std::numeric_limits<std::uint32_t>::max());
    static_assert(offsetof(Sh4ReferenceState, r) <= std::numeric_limits<std::uint32_t>::max());

    std::vector<std::uint8_t> code;
    code.reserve(block.ops.size() * 12u + 1u);
    for (const auto& instruction : block.ops) {
        auto emitted = emit_instruction(code, instruction);
        if (!emitted) {
            return Result<std::vector<std::uint8_t>>::failure(emitted.error, emitted.detail);
        }
    }
    code.push_back(0xC3u); // RET
    return Result<std::vector<std::uint8_t>>::success(std::move(code));
}

Result<void> execute_native_x64_block(std::span<const std::uint8_t> code,
                                      Sh4ReferenceState& state) {
    if (!kX64 || code.empty()) {
        return Result<void>::failure(ErrorCode::backend_unavailable,
                                     "native x64 machine code is unavailable");
    }

#if defined(_WIN32) && (defined(_M_X64) || defined(__x86_64__) || defined(__amd64__))
    void* memory = VirtualAlloc(nullptr, code.size(), MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory == nullptr) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "VirtualAlloc failed for native x64 block");
    }
    std::memcpy(memory, code.data(), code.size());
    DWORD old_protect{};
    if (!VirtualProtect(memory, code.size(), PAGE_EXECUTE_READ, &old_protect)) {
        VirtualFree(memory, 0, MEM_RELEASE);
        return Result<void>::failure(ErrorCode::io_error,
                                     "VirtualProtect failed for native x64 block");
    }
    FlushInstructionCache(GetCurrentProcess(), memory, code.size());
    using NativeBlockFn = void (*)(Sh4ReferenceState*);
    auto fn = reinterpret_cast<NativeBlockFn>(memory);
    fn(&state);
    VirtualFree(memory, 0, MEM_RELEASE);
    return Result<void>::success();
#elif defined(__x86_64__) || defined(__amd64__)
    void* memory = mmap(nullptr,
                        code.size(),
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS,
                        -1,
                        0);
    if (memory == MAP_FAILED) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "mmap failed for native x64 block");
    }
    std::memcpy(memory, code.data(), code.size());
    if (mprotect(memory, code.size(), PROT_READ | PROT_EXEC) != 0) {
        munmap(memory, code.size());
        return Result<void>::failure(ErrorCode::io_error,
                                     "mprotect failed for native x64 block");
    }
    __builtin___clear_cache(static_cast<char*>(memory),
                            static_cast<char*>(memory) + code.size());
    using NativeBlockFn = void (*)(Sh4ReferenceState*);
    auto fn = reinterpret_cast<NativeBlockFn>(memory);
    fn(&state);
    munmap(memory, code.size());
    return Result<void>::success();
#else
    (void)state;
    return Result<void>::failure(ErrorCode::backend_unavailable,
                                 "native x64 machine code requires an x86-64 host");
#endif
}

}
