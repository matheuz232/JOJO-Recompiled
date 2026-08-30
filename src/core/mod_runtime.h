#pragma once

#include "core/result.h"
#include "core/semver.h"

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace jojo {

inline constexpr SemanticVersion kModApiVersion{1, 0, 0};

enum class ModKind {
    data,
    native,
};

struct ModDependency {
    std::string id;
    VersionRequirement requirement;

    friend bool operator==(const ModDependency&, const ModDependency&) = default;
};

struct ModManifest {
    std::string id;
    std::string name;
    SemanticVersion version{};
    SemanticVersion api_version{};
    ModKind kind{ModKind::data};
    bool gameplay{};
    std::filesystem::path entry;
    std::vector<ModDependency> dependencies;
    std::vector<std::string> conflicts;
};

struct DiscoveredMod {
    ModManifest manifest;
    std::filesystem::path root;
};

using ModCatalog = std::vector<DiscoveredMod>;

[[nodiscard]] Result<ModManifest> parse_mod_manifest(
    std::string_view text,
    const std::filesystem::path& root);
[[nodiscard]] Result<ModCatalog> discover_mods(const std::filesystem::path& mods_root);

}
