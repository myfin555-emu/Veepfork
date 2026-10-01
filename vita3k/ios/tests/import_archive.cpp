#include <ios/import_archive.h>
#include <packages/sfo.h>
#include <cassert>
#include <cstring>
#include <filesystem>
#include <iostream>

static void test_sfo() {
    SfoHeader header{0x46535000, 0x101, 36, 48, 1};
    SfoIndexTableEntry entry{0, SfoDataFormat::UTF8_NULL, 10, 10, 0};
    std::vector<uint8_t> valid(58);
    std::memcpy(valid.data(), &header, sizeof(header));
    std::memcpy(valid.data() + sizeof(header), &entry, sizeof(entry));
    std::memcpy(valid.data() + 36, "TITLE_ID", 9);
    std::memcpy(valid.data() + 48, "TEST00001", 10);
    SfoFile parsed;
    assert(sfo::load(parsed, valid));
    std::string title;
    assert(sfo::get_data_by_key(title, parsed, "TITLE_ID") && title == "TEST00001");
    for (size_t size = 0; size < valid.size(); ++size) {
        const std::vector<uint8_t> truncated(valid.begin(), valid.begin() + size);
        assert(!sfo::load(parsed, truncated));
        assert(parsed.entries.empty());
    }
    for (size_t offset : {0, 8, 12, 16, 20, 24, 32}) {
        auto corrupt = valid;
        std::memset(corrupt.data() + offset, 0xff, 4);
        assert(!sfo::load(parsed, corrupt));
    }
    auto empty_string = valid;
    std::memset(empty_string.data() + 24, 0, 4);
    assert(!sfo::load(parsed, empty_string));
    auto no_terminator = valid;
    no_terminator.back() = 'x';
    assert(!sfo::load(parsed, no_terminator));
}

int main(int argc, char **argv) {
    test_sfo();
    assert(argc == 4);
    std::string error;
    const bool expected = std::string(argv[3]) == "ok";
    const bool actual = ios::import::extract_zip(argv[1], argv[2], error);
    if (expected != actual) {
        std::cerr << argv[1] << ": " << error << "\n";
        return 1;
    }
    assert(actual || !error.empty());
    std::cout << std::filesystem::path(argv[1]).filename() << ": OK\n";
}
