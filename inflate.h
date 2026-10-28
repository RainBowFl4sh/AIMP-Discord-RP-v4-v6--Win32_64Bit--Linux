// gzip decoder (RFC 1951 / 1952) for the texts built into the plugin (generated/embedded.h, compressed by CMake).
// Small and slow on purpose: about 150 KB are unpacked once, on first use.
#pragma once
#include <cstddef>
#include <string>

namespace inflate {

// false: no gzip data, damaged data or wrong checksum (out is cleared then)
bool Gunzip(const unsigned char* data, size_t size, std::string& out);

}  // namespace inflate
