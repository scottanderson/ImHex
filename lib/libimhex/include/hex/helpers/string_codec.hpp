#pragma once

#include <hex.hpp>

#include <pl/core/string_encode_decode.hpp>

#include <string>
#include <string_view>
#include <utility>

namespace hex {

    // Decodes and encodes a PatternString's bytes for the pattern language runtime. This is the
    // real value a pattern script sees - through ==, std::print, and everywhere else - not just
    // what the UI draws. See decodeUtf8() and friends in encoding_file.hpp for the
    // separate, fallible decoders the tree view uses for one-line display.
    class ImHexStringCodec : public pl::core::StringEncodeDecode {
    public:
        [[nodiscard]] std::string decode(std::span<const u8> bytes, std::string_view encoding) const override;
        [[nodiscard]] std::vector<u8> encode(std::string_view text, std::string_view encoding) const override;
    };

    // Replaces a character `encoding` cannot represent with '?'. Returns the replaced text, and
    // whether a replacement happened. For UI editing only - a script's own write still throws.
    [[nodiscard]] std::pair<std::string, bool> sanitizeForEncoding(std::string_view text, std::string_view encoding);

}
