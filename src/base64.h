#pragma once

#include <string>
#include <string_view>

namespace datatransfer {

std::string Base64Encode(std::string_view input);
std::string Base64Decode(std::string_view input);

} // namespace datatransfer
