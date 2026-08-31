#include "core/conversion.h"
#include "core/disc_image.h"
#include "core/psx_boot.h"
#include "core/psx_revision.h"
#include "core/version.h"
#include <charconv>
#include <fstream>
#include <system_error>
#include <vector>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#endif

namespace jojo {
namespace {
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
    return Result<void>::failure(ErrorCode::io_error, "failed to replace prepared file");
#else
    std::error_code ec;
    std::filesystem::rename(temp, target, ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to replace prepared file: " + ec.message());
    }
    return Result<void>::success();
#endif
}

Result<void> save_binary_atomic(const std::filesystem::path& path,
                                const std::vector<std::uint8_t>& bytes) {
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return Result<void>::failure(ErrorCode::io_error,
                                     "failed to create prepared-data directory: " + ec.message());
    }

    auto temp = path;
    temp += ".tmp";
    {
        std::ofstream out(temp, std::ios::binary | std::ios::trunc);
        if (!out) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed to create temporary prepared file");
        }
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        out.flush();
        if (!out) {
            return Result<void>::failure(ErrorCode::io_error,
                                         "failed while writing prepared file");
        }
    }
    return replace_file(temp, path);
}
}

Result<void> save_conversion_manifest_atomic(const std::filesystem::path& path,
                                             const ConversionManifest& m) {
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
    }
    if (m.manifest_version != "1" || m.converter_version.empty() || m.source_name.empty() ||
        m.source_format.empty() || m.hash_hex.empty() || m.backend.empty()) {
        return Result<ConversionManifest>::failure(ErrorCode::invalid_installation,
                                                   "manifest is missing required fields");
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

    std::string psx_executable_path;
    if (options.validate_psx_boot) {
        auto boot = analyze_psx_boot(filesystem.value);
        if (!boot) {
            return Result<ConversionManifest>::failure(boot.error, boot.detail);
        }
        psx_executable_path = boot.value.executable_path;
    }

    report(ConversionStage::identifying_revision, 45, "identify_revision",
           "Identificando a revisão exata do jogo.");
    auto revision = identify_game_revision(filesystem.value, options.revision_profiles);
    if (!revision) {
        return Result<ConversionManifest>::failure(revision.error, revision.detail);
    }

    report(ConversionStage::preparing_installation, 65, "prepare_installation",
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

    if (options.validate_psx_boot) {
        report(ConversionStage::preparing_installation, 75, "materialize_psx_runtime",
               "Extraindo os arquivos de boot PS1 validados para a instalação local.");

        auto system_file = read_iso9660_file(filesystem.value, "/SYSTEM.CNF");
        if (!system_file) {
            return Result<ConversionManifest>::failure(system_file.error, system_file.detail);
        }
        auto executable_file = read_iso9660_file(filesystem.value, psx_executable_path);
        if (!executable_file) {
            return Result<ConversionManifest>::failure(executable_file.error, executable_file.detail);
        }

        auto saved_system = save_binary_atomic(install_dir / "data" / "SYSTEM.CNF",
                                               system_file.value);
        if (!saved_system) {
            return Result<ConversionManifest>::failure(saved_system.error, saved_system.detail);
        }
        auto saved_executable = save_binary_atomic(install_dir / "data" / "PSX.EXE",
                                                   executable_file.value);
        if (!saved_executable) {
            return Result<ConversionManifest>::failure(saved_executable.error,
                                                       saved_executable.detail);
        }
        manifest.backend = "psx-runtime-prepared";
    }

    report(ConversionStage::writing_manifest, 90, "write_manifest",
           "Gravando os metadados da instalação.");
    auto saved = save_conversion_manifest_atomic(install_dir / "game_manifest.ini", manifest);
    if (!saved) return Result<ConversionManifest>::failure(saved.error, saved.detail);

    report(ConversionStage::completed, 100, "conversion_complete",
           options.validate_psx_boot
               ? "Preparação PS1 concluída; os arquivos de boot validados estão disponíveis localmente."
               : "Preparação base concluída; o backend específico do jogo ainda será adicionado.");
    return Result<ConversionManifest>::success(std::move(manifest));
}

Result<ConversionManifest> convert_image(const std::filesystem::path& source,
                                         const std::filesystem::path& install_dir,
                                         const ConversionProgressCallback& on_progress) {
    ConversionOptions options{};
    options.revision_profiles = supported_psx_game_revision_profiles();
    options.validate_psx_boot = true;
    return convert_image(source, install_dir, options, on_progress);
}

}
