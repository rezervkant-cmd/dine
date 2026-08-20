#pragma once
#include "core/types.hpp"
#include <filesystem>

namespace vw::zip {

struct Entry {
    std::string name;
    u64 comp_size = 0;
    u64 uncomp_size = 0;
    u64 local_header_offset = 0;
    u32 crc32 = 0;
    u16 method = 0;
};

class ZipFile {
public:
    explicit ZipFile(const std::filesystem::path& path);

    const std::vector<Entry>& entries() const { return entries_; }
    const Entry* find(std::string_view name) const;

    std::vector<u8> read(const Entry& e) const;
    std::optional<std::vector<u8>> read(std::string_view name) const;

private:
    void parse_central_directory();
    std::vector<u8> data_;
    std::vector<Entry> entries_;
    std::unordered_map<std::string, size_t> index_;
};

}
