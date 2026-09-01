#pragma once

namespace jojo {

struct PsxBus;

void psx_gpu_execute_textured_rectangle(PsxBus& bus) noexcept;

} // namespace jojo
