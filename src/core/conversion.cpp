#include "core/conversion.h"
#include "core/disc_image.h"
#include "core/game_backend.h"
#include "core/version.h"
#include <array>
#include <charconv>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace jojo {
namespace {
struct ObservedDiscRevision {
    std::string_view source_format;
    std::uint64_t source_size;
    std::string_view hash_hex;
    std::string_view revision_id;
};

constexpr std::array<ObservedDiscRevision, 1> observed_disc_revisions{{
    {"bin", 666806112ull, "b8b5dbf79cdb9fcf", "jojo-usa-observed-b8b5dbf79cdb9fcf"},
}};

std::string trim(std::string s) {
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return s.substr(first, last - first + 1);
}

Result<std::uint64_t> parse_u64(const std::string& text) {
    std::uint64_t value{};
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return Result<std::uint64_t>::failure(ErrorCode::invalid_installation,
                                              "invalid unsigned integer in manifest: " + text);
    }
    return Result<std::uint64_t>::success(value);
}

Result<void> replace_file(const std::filesystem::path& temp,
                          const std::filesystem::path& target) {
#ifdef _WIN32
    if (MoveFileExW(temp.c_str(), target.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        return Result<void>::success();
    }
    return Result<void>::failure(ErrorCode::io_error, "failed to replace manifest file");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to replace manifest file: " + ec.message());
    }
    return Result<void>::success();
#endif
}
}

bool has_complete_native_backend_metadata(const ConversionManifest& m) noexcept {
    if (m.boot_program_hash_hex.empty() ||
        !m.backend_abi_version.has_value() ||
        m.backend_program_hash.empty() ||
        !m.backend_block_count.has_value() ||
        !m.backend_native_block_count.has_value() ||
        !m.backend_fallback_block_count.has_value() ||
        !m.backend_native_code_bytes.has_value()) {
        return false;
    }
    return *m.backend_native_block_count <= *m.backend_block_count &&
           *m.backend_fallback_block_count ==
               *m.backend_block_count - *m.backend_native_block_count;
}

Result<GameRevisionMatch> identify_observed_disc_revision(
    std::string_view source_format,
    std::uint64_t source_size,
    std::string_view hash_hex) {
    for (const auto& observed : observed_disc_revisions) {
        if (observed.source_format == source_format &&
            observed.source_size == source_size &&
            observed.hash_hex == hash_hex) {
            return Result<GameRevisionMatch>::success(
                GameRevisionMatch{std::string(observed.revision_id)});
        }
    }
    return Result<GameRevisionMatch>::failure(
        ErrorCode::unknown_revision,
        "disc fingerprint does not match any observed game revision");
}

Result<void> save_conversion_manifest_atomic(const std::filesystem::path& path,
                                             const ConversionManifest& m) {
    if (m.backend == "native-ready" && !has_complete_native_backend_metadata(m)) {
        return Result<void>::failure(
            ErrorCode::invalid_installation,
            "native-ready manifest is missing complete native backend metadata");
    }

    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to create manifest directory: " + ec.message());
    }
    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::trunc);
        if (!out) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed to create temporary manifest");
        }
        out << "manifest_version=" << m.manifest_version << '\n';
        out << "converter_version=" << m.converter_version << '\n';
        out << "source_name=" << m.source_name << '\n';
        out << "source_format=" << m.source_format << '\n';
        out << "source_size=" << m.source_size << '\n';
        out << "hash_fnv1a64=" << m.hash_hex << '\n';
        out << "revision_id=" << m.revision_id << '\n';
        out << "backend=" << m.backend << '\n';
        if (!m.boot_program_hash_hex.empty()) {
            out << "boot_program_hash_fnv1a64=" << m.boot_program_hash_hex << '\n';
        }
        if (m.backend_abi_version.has_value()) {
            out << "backend_abi_version=" << *m.backend_abi_version << '\n';
        }
        if (!m.backend_program_hash.empty()) {
            out << "backend_program_hash=" << m.backend_program_hash << '\n';
        }
        if (m.backend_block_count.has_value()) {
            out << "backend_block_count=" << *m.backend_block_count << '\n';
        }
        if (m.backend_native_block_count.has_value()) {
            out << "backend_native_block_count=" << *m.backend_native_block_count << '\n';
        }
        if (m.backend_fallback_block_count.has_value()) {
            out << "backend_fallback_block_count=" << *m.backend_fallback_block_count << '\n';
        }
        if (m.backend_native_code_bytes.has_value()) {
            out << "backend_native_code_bytes=" << *m.backend_native_code_bytes << '\n';
        }
        out.flush();
        if (!out) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed while writing manifest");
        }
    }
    return replace_file(temp, path);
}

Result<ConversionManifest> load_conversion_manifest(const std::filesystem::path& path) {
    std::ifstream in(path);
    if (!in) {
        return Result<ConversionManifest>::failure(ErrorCode::file_not_found,
                                                   "game manifest not found: " + path.string());
    }
    ConversionManifest m{};
    std::string line;
    while (std::getline(in, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#' || line.front() == ';') continue;
        const auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        const auto key = trim(line.substr(0, eq));
        const auto value = trim(line.substr(eq + 1));
        if (key == "manifest_version") m.manifest_version = value;
        else if (key == "converter_version") m.converter_version = value;
        else if (key == "source_name") m.source_name = value;
        else if (key == "source_format") m.source_format = value;
        else if (key == "source_size") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.source_size = parsed.value;
        } else if (key == "hash_fnv1a64") m.hash_hex = value;
        else if (key == "revision_id") m.revision_id = value;
        else if (key == "backend") m.backend = value;
        else if (key == "boot_program_hash_fnv1a64") m.boot_program_hash_hex = value;
        else if (key == "backend_abi_version") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            if (parsed.value > std::numeric_limits<std::uint32_t>::max()) {
                return Result<ConversionManifest>::failure(
                    ErrorCode::invalid_installation,
                    "backend ABI version exceeds uint32 range");
            }
            m.backend_abi_version = static_cast<std::uint32_t>(parsed.value);
        } else if (key == "backend_program_hash") m.backend_program_hash = value;
        else if (key == "backend_block_count") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.backend_block_count = parsed.value;
        } else if (key == "backend_native_block_count") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.backend_native_block_count = parsed.value;
        } else if (key == "backend_fallback_block_count") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.backend_fallback_block_count = parsed.value;
        } else if (key == "backend_native_code_bytes") {
            auto parsed = parse_u64(value);
            if (!parsed) return Result<ConversionManifest>::failure(parsed.error, parsed.detail);
            m.backend_native_code_bytes = parsed.value;
        }
    }
    if (m.manifest_version != "1" || m.converter_version.empty() || m.source_name.empty() ||
        m.source_format.empty() || m.hash_hex.empty() || m.backend.empty()) {
        return Result<ConversionManifest>::failure(ErrorCode::invalid_installation,
                                                   "manifest is missing required fields");
    }
    if (m.backend == "native-ready" && !has_complete_native_backend_metadata(m)) {
        return Result<ConversionManifest>::failure(
            ErrorCode::invalid_installation,
            "native-ready manifest is missing complete native backend metadata");
    }
    return Result<ConversionManifest>::success(std::move(m));
}

Result<ConversionManifest> convert_image(const std::filesystem::path& source,
                                         const std::filesystem::path& install_dir,
                                         const ConversionOptions& options,
                                         const ConversionProgressCallback& on_progress) {
    const auto report = [&](ConversionStage stage, int percent,
                            std::string message_key, std::string detail) {
        if (on_progress) {
            on_progress(ConversionProgress{stage, percent, std::move(message_key), std::move(detail)});
        }
    };

    report(ConversionStage::validating_source, 0, "validate_source",
           "Validando a imagem selecionada.");
    if (install_dir.empty()) {
        return Result<ConversionManifest>::failure(ErrorCode::invalid_argument,
                                                   "installation directory cannot be empty");
    }
    if (!supported_disc_extension(source.string())) {
        return Result<ConversionManifest>::failure(ErrorCode::unsupported_format,
                                                   "unsupported game image format");
    }

    report(ConversionStage::fingerprinting_source, 15, "fingerprint_source",
           "Calculando a identificação da imagem.");
    auto fp = fingerprint_disc_image(source);
    if (!fp) return Result<ConversionManifest>::failure(fp.error, fp.detail);

    report(ConversionStage::discovering_filesystem, 30, "discover_filesystem",
           "Lendo o sistema de arquivos da mídia em modo somente leitura.");
    auto filesystem = open_iso9660(source);
    if (!filesystem) {
        return Result<ConversionManifest>::failure(filesystem.error, filesystem.detail);
    }

    report(ConversionStage::identifying_revision, 45, "identify_revision",
           "Identificando a revisão exata do jogo.");
    auto revision = identify_game_revision(filesystem.value, options.revision_profiles);
    if (!revision && revision.error == ErrorCode::unknown_revision) {
        auto observed = identify_observed_disc_revision(
            fp.value.format, fp.value.size_bytes, fp.value.hash_hex);
        if (observed) {
            revision = std::move(observed);
            report(ConversionStage::identifying_revision, 45, "revision_observed",
                   "Revisão observada reconhecida pelo fingerprint completo da imagem.");
        }
    }
    if (!revision) {
        const bool may_prepare_unverified =
            options.allow_unverified_base_conversion &&
            options.revision_profiles.empty() &&
            revision.error == ErrorCode::unknown_revision;
        if (!may_prepare_unverified) {
            return Result<ConversionManifest>::failure(revision.error, revision.detail);
        }
        revision = Result<GameRevisionMatch>::success(
            GameRevisionMatch{"unverified-fnv1a64-" + fp.value.hash_hex});
        report(ConversionStage::identifying_revision, 45, "revision_unverified",
               "Revisão ainda não verificada; continuando somente com a preparação base.");
    }

    report(ConversionStage::preparing_installation, 55, "prepare_installation",
           "Preparando os diretórios da instalação convertida.");
    std::error_code ec;
    std::filesystem::create_directories(install_dir / "data", ec);
    if (ec) return Result<ConversionManifest>::failure(ErrorCode::io_error, ec.message());
    std::filesystem::create_directories(install_dir / "cache", ec);
    if (ec) return Result<ConversionManifest>::failure(ErrorCode::io_error, ec.message());
    std::filesystem::create_directories(install_dir / "logs", ec);
    if (ec) return Result<ConversionManifest>::failure(ErrorCode::io_error, ec.message());

    ConversionManifest manifest{};
    manifest.converter_version = core_version();
    manifest.source_name = source.filename().string();
    manifest.source_format = fp.value.format;
    manifest.source_size = fp.value.size_bytes;
    manifest.hash_hex = fp.value.hash_hex;
    manifest.revision_id = revision.value.revision_id;

    report(ConversionStage::writing_manifest, 55, "write_pending_manifest",
           "Gravando o estado pendente antes de preparar o backend específico do jogo.");
    auto saved = save_conversion_manifest_atomic(install_dir / "game_manifest.ini", manifest);
    if (!saved) return Result<ConversionManifest>::failure(saved.error, saved.detail);

    if (!supports_game_native_backend(manifest.revision_id)) {
        report(ConversionStage::completed, 100, "conversion_complete",
               "Preparação base concluída; o backend específico do jogo ainda será adicionado.");
        return Result<ConversionManifest>::success(std::move(manifest));
    }

    GameBackendProgressCallback backend_progress = [&](GameBackendStage stage) {
        switch (stage) {
            case GameBackendStage::boot_analyzed:
                report(ConversionStage::preparing_game_backend, 65,
                       "analyze_game_boot",
                       "Programa de boot Dreamcast analisado para a revisão reconhecida.");
                break;
            case GameBackendStage::cache_ready:
                report(ConversionStage::building_native_backend, 80,
                       "build_native_backend",
                       "Backend nativo gerado ou reutilizado para o programa identificado.");
                break;
            case GameBackendStage::cache_verified:
                report(ConversionStage::verifying_native_backend, 92,
                       "verify_native_backend",
                       "Cache do backend nativo recarregado e verificado.");
                break;
        }
    };

    auto prepared = prepare_game_native_backend(
        manifest.revision_id, filesystem.value, install_dir, backend_progress);
    if (!prepared) {
        return Result<ConversionManifest>::failure(prepared.error, prepared.detail);
    }

    manifest.boot_program_hash_hex = prepared.value.boot_program_hash_hex;
    manifest.backend_abi_version = prepared.value.abi_version;
    manifest.backend_program_hash = prepared.value.program_hash;
    manifest.backend_block_count = prepared.value.block_count;
    manifest.backend_native_block_count = prepared.value.native_block_count;
    manifest.backend_fallback_block_count = prepared.value.fallback_block_count;
    manifest.backend_native_code_bytes = prepared.value.native_code_bytes;
    manifest.backend = "native-ready";

    report(ConversionStage::promoting_native_backend, 97, "promote_native_backend",
           "Promovendo a instalação após verificar o backend nativo.");
    saved = save_conversion_manifest_atomic(install_dir / "game_manifest.ini", manifest);
    if (!saved) return Result<ConversionManifest>::failure(saved.error, saved.detail);

    report(ConversionStage::completed, 100, "conversion_complete",
           "Backend nativo da revisão reconhecida preparado; validação fim a fim é o próximo marco.");
    return Result<ConversionManifest>::success(std::move(manifest));
}

Result<ConversionManifest> convert_image(const std::filesystem::path& source,
                                         const std::filesystem::path& install_dir,
                                         const ConversionProgressCallback& on_progress) {
    ConversionOptions options{};
    options.allow_unverified_base_conversion = true;
    return convert_image(source, install_dir, options, on_progress);
}

}
