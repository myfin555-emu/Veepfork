#include <ios/import_archive.h>

#include <miniz.h>

#include <cstring>
#include <vector>

namespace ios::import {
namespace {
struct ZipReader {
    mz_zip_archive archive{};
    ~ZipReader() { mz_zip_reader_end(&archive); }
};

bool safe_path(const std::string &name) {
    if (name.empty() || name.front() == '/' || name.find('\\') != std::string::npos || name.find(':') != std::string::npos)
        return false;
    for (const auto &part : std::filesystem::path(name)) {
        if (part == "..")
            return false;
    }
    return true;
}
}

bool extract_zip(const std::filesystem::path &source, const std::filesystem::path &destination, std::string &error) {
    ZipReader reader;
    auto *zip = &reader.archive;
    auto fail = [&](const std::string &message) {
        error = message + ": " + mz_zip_get_error_string(mz_zip_get_last_error(zip));
        return false;
    };
    if (!mz_zip_reader_init_file(zip, source.c_str(), 0))
        return fail("Não foi possível abrir o ZIP/VPK");

    std::filesystem::create_directories(destination);
    auto available = std::filesystem::space(destination).available;
    const auto count = mz_zip_reader_get_num_files(zip);
    // Validate every entry and the total disk requirement before extracting.
    for (mz_uint i = 0; i < count; ++i) {
        mz_zip_archive_file_stat info{};
        if (!mz_zip_reader_file_stat(zip, i, &info))
            return fail("Entrada inválida no arquivo");
        const auto length = mz_zip_reader_get_filename(zip, i, nullptr, 0);
        std::vector<char> filename(length);
        if (length < 2 || mz_zip_reader_get_filename(zip, i, filename.data(), length) != length
            || std::strlen(filename.data()) != length - 1 || !safe_path(filename.data())) {
            error = "O ZIP/VPK contém um caminho inválido.";
            return false;
        }
        if (!info.m_is_supported || info.m_is_encrypted) {
            error = "Compressão não suportada ou arquivo protegido por senha: " + std::string(filename.data());
            return false;
        }
        if (info.m_uncomp_size > available) {
            error = "Espaço insuficiente para descompactar o ZIP/VPK.";
            return false;
        }
        available -= info.m_uncomp_size;
    }
    for (mz_uint i = 0; i < count; ++i) {
        const auto length = mz_zip_reader_get_filename(zip, i, nullptr, 0);
        std::vector<char> filename(length);
        mz_zip_reader_get_filename(zip, i, filename.data(), length);
        const auto target = destination / filename.data();
        if (mz_zip_reader_is_file_a_directory(zip, i)) {
            std::filesystem::create_directories(target);
            continue;
        }
        std::filesystem::create_directories(target.parent_path());
        // miniz uses fixed-size read/inflate buffers and checks size + CRC.
        if (!mz_zip_reader_extract_to_file(zip, i, target.c_str(), 0))
            return fail("Falha ao extrair " + std::string(filename.data()));
    }
    return true;
}
}
