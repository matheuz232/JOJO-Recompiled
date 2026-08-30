#include "core/mod_runtime.h"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
int failures = 0;
#define CHECK(...) do { if (!(static_cast<bool>(__VA_ARGS__))) { std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #__VA_ARGS__ "\n"; ++failures; } } while (0)

void write_text(const std::filesystem::path& path, const std::string& text) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

std::string data_manifest(std::string id, std::string version = "1.0.0") {
    return "id=" + id + "\n"
           "name=Test Mod\n"
           "version=" + version + "\n"
           "api_version=1.0.0\n"
           "kind=data\n"
           "gameplay=false\n";
}
}

int main() {
    using namespace jojo;

    const auto parsed = parse_mod_manifest(
        "# comment\n"
        "id=example.ui\n"
        "name=Example UI\n"
        "version=1.2.3\n"
        "api_version=1.0.0\n"
        "kind=data\n"
        "gameplay=false\n"
        "depends=base.assets@>=1.0.0, shared.lib@^2.1.0\n"
        "conflicts=legacy.ui, broken.ui\n",
        std::filesystem::path("mods/example"));
    CHECK(parsed);
    if (parsed) {
        CHECK(parsed.value.id == "example.ui");
        CHECK(parsed.value.name == "Example UI");
        CHECK(parsed.value.version == SemanticVersion{1, 2, 3});
        CHECK(parsed.value.api_version == SemanticVersion{1, 0, 0});
        CHECK(parsed.value.kind == ModKind::data);
        CHECK(!parsed.value.gameplay);
        CHECK(parsed.value.entry.empty());
        CHECK(parsed.value.dependencies.size() == 2u);
        CHECK(parsed.value.dependencies[0].id == "base.assets");
        CHECK(parsed.value.dependencies[0].requirement.kind == VersionRequirementKind::at_least);
        CHECK(parsed.value.dependencies[1].requirement.kind == VersionRequirementKind::compatible_major);
        CHECK(parsed.value.conflicts.size() == 2u);
    }

    const auto native = parse_mod_manifest(
        "id=native.sample\n"
        "name=Native Sample\n"
        "version=3.4.5\n"
        "api_version=1.0.0\n"
        "kind=native\n"
        "gameplay=1\n"
        "entry=bin/sample_mod.dll\n",
        std::filesystem::path("mods/native"));
    CHECK(native);
    if (native) {
        CHECK(native.value.kind == ModKind::native);
        CHECK(native.value.gameplay);
        CHECK(native.value.entry.generic_string() == "bin/sample_mod.dll");
    }

    CHECK(!parse_mod_manifest(data_manifest("Upper.Case"), "mods/bad"));
    CHECK(!parse_mod_manifest(data_manifest("-bad"), "mods/bad"));
    CHECK(!parse_mod_manifest(data_manifest("bad id"), "mods/bad"));
    CHECK(!parse_mod_manifest(
        data_manifest("bad.unknown") + "mystery=1\n", "mods/bad"));
    CHECK(!parse_mod_manifest(
        "id=native.noentry\nname=X\nversion=1.0.0\napi_version=1.0.0\nkind=native\ngameplay=0\n", "mods/bad"));
    CHECK(!parse_mod_manifest(
        data_manifest("bad.entry") + "entry=plugin.dll\n", "mods/bad"));
    CHECK(!parse_mod_manifest(
        "id=native.escape\nname=X\nversion=1.0.0\napi_version=1.0.0\nkind=native\ngameplay=0\nentry=../evil.dll\n", "mods/bad"));
    CHECK(!parse_mod_manifest(
        "id=future.api\nname=X\nversion=1.0.0\napi_version=2.0.0\nkind=data\ngameplay=0\n", "mods/bad"));
    CHECK(!parse_mod_manifest(
        "id=future.minor\nname=X\nversion=1.0.0\napi_version=1.1.0\nkind=data\ngameplay=0\n", "mods/bad"));

    const auto root = std::filesystem::temp_directory_path() / "jojo_mod_discovery_tests";
    std::error_code ec;
    std::filesystem::remove_all(root, ec);
    write_text(root / "zeta" / "mod.ini", data_manifest("zeta.mod"));
    write_text(root / "alpha" / "mod.ini", data_manifest("alpha.mod"));
    std::filesystem::create_directories(root / "ignored-no-manifest");

    const auto catalog = discover_mods(root);
    CHECK(catalog);
    if (catalog) {
        CHECK(catalog.value.size() == 2u);
        CHECK(catalog.value[0].manifest.id == "alpha.mod");
        CHECK(catalog.value[1].manifest.id == "zeta.mod");
    }

    write_text(root / "duplicate" / "mod.ini", data_manifest("alpha.mod", "2.0.0"));
    const auto duplicate = discover_mods(root);
    CHECK(!duplicate);
    if (!duplicate) {
        CHECK(duplicate.detail.find("alpha.mod") != std::string::npos);
        CHECK(duplicate.detail.find("duplicate") != std::string::npos);
    }

    std::filesystem::remove_all(root, ec);

    if (failures != 0) {
        std::cerr << failures << " mod runtime test(s) failed\n";
        return 1;
    }
    std::cout << "mod manifest/discovery tests passed\n";
    return 0;
}
