#include "Base64.h"
#include <array>
#include <cstdint>
#include <cstring>
#include <algorithm>

namespace Ashes {

constexpr unsigned char kBase64Chars[] = {
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789+/"};

static const auto kBase64Values = []() {
    std::array<std::uint8_t, 256> values;
    std::memset(values.data(), 0, values.size());
    for (std::uint8_t i = 0; i < std::size(kBase64Chars); ++i)
        values[kBase64Chars[i]] = i;
    return values; }();

static void Base64DecodeImpl(const unsigned char* in, std::size_t n, std::string& out)
{
    out.reserve((n / 4 + 1) * 3);
    unsigned int bits = 0;
    unsigned int num_bits = 0;
    for (std::size_t i = 0; i < n; ++i)
    {
        bits = (bits << 6) | kBase64Values[in[i]];
        if ((num_bits += 6) >= 8)
            out.push_back(static_cast<char>((bits >> (num_bits -= 8)) & 0xFF));
    }
}

void Base64Decode(const void* in, std::size_t n, std::string& out)
{
    auto first = static_cast<const unsigned char*>(in);
    auto last = std::find_if(first, first + n, [](unsigned char c) {
        return kBase64Chars[kBase64Values[c]] != c; });
    Base64DecodeImpl(first, last - first, out);
}

}