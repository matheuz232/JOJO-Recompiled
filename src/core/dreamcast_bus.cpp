#include "core/dreamcast_bus.h"

namespace jojo {

Result<std::uint8_t> DreamcastReferenceBus::read8(std::uint32_t address) {
    if (memory_ == nullptr) {
        return Result<std::uint8_t>::failure(
            ErrorCode::invalid_installation,
            "Dreamcast reference bus has no memory backing");
    }
    return read_dreamcast_u8(*memory_, address);
}

Result<void> DreamcastReferenceBus::write8(std::uint32_t address, std::uint8_t value) {
    if (memory_ == nullptr) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "Dreamcast reference bus has no memory backing");
    }
    return write_dreamcast_u8(*memory_, address, value);
}

}
