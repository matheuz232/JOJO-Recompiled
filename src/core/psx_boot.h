#pragma once
#include "core/iso9660.h"
#include "core/psx_exe.h"
#include "core/psx_system_cnf.h"
#include "core/result.h"
#include <string>

namespace jojo {

struct PsxBootImage {
    PsxSystemCnf system;
    PsxExeHeader executable;
    std::string executable_path;
};

[[nodiscard]] Result<PsxBootImage> analyze_psx_boot(const Iso9660Image& image);

}
