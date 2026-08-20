#pragma once
#include "core/types.hpp"

namespace vw::anvil {

// LZ4BlockOutputStream (region compression type 4).
std::vector<u8> decode_lz4_block_stream(std::span<const u8> source);

}
