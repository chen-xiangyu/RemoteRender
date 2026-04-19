#include "base64.h"

#include <array>
#include <stdexcept>

namespace datatransfer {

namespace {

constexpr std::string_view kAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

} // namespace

std::string Base64Encode(std::string_view input) {
    std::string output;
    output.reserve(((input.size() + 2U) / 3U) * 4U);

    for (std::size_t i = 0; i < input.size(); i += 3U) {
        const auto remain = input.size() - i;
        const unsigned char a = static_cast<unsigned char>(input[i]);
        const unsigned char b = remain > 1U ? static_cast<unsigned char>(input[i + 1U]) : 0U;
        const unsigned char c = remain > 2U ? static_cast<unsigned char>(input[i + 2U]) : 0U;

        const unsigned value =
            (static_cast<unsigned>(a) << 16U) |
            (static_cast<unsigned>(b) << 8U) |
            static_cast<unsigned>(c);

        output.push_back(kAlphabet[(value >> 18U) & 0x3FU]);
        output.push_back(kAlphabet[(value >> 12U) & 0x3FU]);
        output.push_back(remain > 1U ? kAlphabet[(value >> 6U) & 0x3FU] : '=');
        output.push_back(remain > 2U ? kAlphabet[value & 0x3FU] : '=');
    }

    return output;
}

std::string Base64Decode(std::string_view input) {
    std::array<int, 256> table{};
    table.fill(-1);
    for (int i = 0; i < static_cast<int>(kAlphabet.size()); ++i) {
        table[static_cast<unsigned char>(kAlphabet[static_cast<std::size_t>(i)])] = i;
    }

    std::string output;
    output.reserve((input.size() / 4U) * 3U);

    int value = 0;
    int bits = -8;
    for (const char ch : input) {
        if (ch == '=') {
            break;
        }

        const int decoded = table[static_cast<unsigned char>(ch)];
        if (decoded < 0) {
            throw std::runtime_error("Invalid base64 input");
        }

        value = (value << 6) + decoded;
        bits += 6;
        if (bits >= 0) {
            output.push_back(static_cast<char>((value >> bits) & 0xFF));
            bits -= 8;
        }
    }

    return output;
}

} // namespace datatransfer
