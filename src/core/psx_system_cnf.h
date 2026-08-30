#pragma once
#include "core/result.h"
#include <cstdint>
#include <string>
#include <string_view>

namespace jojo {

struct PsxSystemCnf {
    std::string boot_iso_path;
    std::uint32_t tcb{};
    std::uint32_t event{};
    std::uint32_t stack{};
};

[[nodiscard]] Result<PsxSystemCnf> parse_psx_system_cnf(std::string_view text);

}
