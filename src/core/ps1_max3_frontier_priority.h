#pragma once

#include "core/ps1_max3_explorer.h"

#include <vector>

namespace jojo {

[[nodiscard]] std::vector<Ps1Max3FrontierCluster>
cluster_and_rank_ps1_max3_frontiers(const Ps1Max3Report& report);

} // namespace jojo
