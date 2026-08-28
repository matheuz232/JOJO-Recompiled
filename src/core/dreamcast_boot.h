#pragma once
#include "core/iso9660.h"
#include "core/result.h"
#include <cstdint>
#include <string>
#include <vector>

namespace jojo {

struct DreamcastIpMetadata {
    std::string hardware_id;
    std::string maker_id;
    std::string device_info;
    std::string area_symbols;
    std::string peripherals;
    std::string product_number;
    std::string product_version;
    std::string release_field;
    std::string boot_filename;
    std::string company_name;
    std::string software_name;
};

struct DreamcastBootProgram {
    DreamcastIpMetadata metadata;
    std::vector<std::uint8_t> bytes;
    std::uint64_t fnv1a64{};
    std::string hash_hex;
};

[[nodiscard]] Result<DreamcastIpMetadata> read_dreamcast_ip_metadata(
    const Iso9660Image& image);

[[nodiscard]] Result<DreamcastBootProgram> read_dreamcast_boot_program(
    const Iso9660Image& image,
    std::uint64_t max_program_bytes = 64ull * 1024ull * 1024ull);

}
