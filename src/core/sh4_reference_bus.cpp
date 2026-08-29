#include "core/sh4_reference_executor.h"

#include <limits>

namespace jojo {

std::uint8_t Sh4ReferenceBytes::read(std::size_t index) const {
    if (bus_ != nullptr) {
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            throw Sh4ReferenceBusFailure{
                ErrorCode::invalid_argument,
                "reference bus address exceeds SH-4 address width"};
        }
        auto value = bus_->read8(static_cast<std::uint32_t>(index));
        if (!value) throw Sh4ReferenceBusFailure{value.error, value.detail};
        return value.value;
    }
    if (index >= flat_.size()) {
        throw Sh4ReferenceBusFailure{
            ErrorCode::invalid_argument,
            "reference flat-memory byte access is outside the mapped range"};
    }
    return flat_[index];
}

void Sh4ReferenceBytes::write(std::size_t index, std::uint8_t value) {
    if (bus_ != nullptr) {
        if (index > std::numeric_limits<std::uint32_t>::max()) {
            throw Sh4ReferenceBusFailure{
                ErrorCode::invalid_argument,
                "reference bus address exceeds SH-4 address width"};
        }
        auto stored = bus_->write8(static_cast<std::uint32_t>(index), value);
        if (!stored) throw Sh4ReferenceBusFailure{stored.error, stored.detail};
        return;
    }
    if (index >= flat_.size()) {
        throw Sh4ReferenceBusFailure{
            ErrorCode::invalid_argument,
            "reference flat-memory byte access is outside the mapped range"};
    }
    flat_[index] = value;
}

Sh4ReferenceByteProxy::operator std::uint8_t() const {
    return owner_->read(index_);
}

Sh4ReferenceByteProxy& Sh4ReferenceByteProxy::operator=(std::uint8_t value) {
    owner_->write(index_, value);
    return *this;
}

Result<Sh4ReferenceRunResult> execute_sh4_ir_reference(
    const Sh4IrProgram& program,
    Sh4ReferenceState& state,
    Sh4ReferenceBus& bus,
    std::size_t max_blocks) {
    try {
        return execute_sh4_ir_reference(
            program,
            state,
            Sh4ReferenceMemoryView::for_bus(bus),
            max_blocks);
    } catch (const Sh4ReferenceBusFailure& failure) {
        return Result<Sh4ReferenceRunResult>::failure(failure.error, failure.detail);
    }
}

}
