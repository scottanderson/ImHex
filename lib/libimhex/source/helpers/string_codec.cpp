#include <hex/helpers/string_codec.hpp>
#include <hex/helpers/encoding_file.hpp>
#include <hex/helpers/fmt.hpp>
#include <hex/api/imhex_api/hex_editor.hpp>

#include <algorithm>
#include <stdexcept>

namespace hex {

    namespace {

        // Resolves to `encoding` if non-empty, else the document's declared encoding, else
        // UTF-8. Same order as the tree view's formatValueWithEncoding().
        std::string resolveEncodingName(std::string_view encoding) {
            if (!encoding.empty())
                return std::string(encoding);

            if (const auto &declaredEncoding = ImHexApi::HexEditor::getEncodingName(); declaredEncoding.has_value())
                return *declaredEncoding;

            return "UTF-8";
        }

        size_t utf8CharLength(u8 leadByte) {
            if ((leadByte & 0x80) == 0x00) return 1;
            if ((leadByte & 0xE0) == 0xC0) return 2;
            if ((leadByte & 0xF0) == 0xE0) return 3;
            if ((leadByte & 0xF8) == 0xF0) return 4;
            return 1;
        }

    }

    std::pair<std::string, bool> sanitizeForEncoding(std::string_view text, std::string_view encoding) {
        const auto name = resolveEncodingName(encoding);

        if (name == "UTF-8" || name.starts_with("UTF-16") || name.starts_with("UTF-32"))
            return { std::string(text), false };

        const auto *table = getEncodingByName(name);
        if (table == nullptr)
            return { std::string(text), false };

        // U+FFFD when this encoding has a byte for it, '?' otherwise. Most single byte
        // codepages have no byte for U+FFFD at all.
        const std::string_view replacement = table->getBytesFor("\xEF\xBF\xBD").has_value() ? "\xEF\xBF\xBD" : "?";

        std::string result;
        bool replaced = false;

        while (!text.empty()) {
            if (auto match = table->getBytesFor(text); match.has_value()) {
                result += text.substr(0, match->second);
                text = text.substr(match->second);
                continue;
            }

            replaced = true;
            result += replacement;
            text = text.substr(std::min(utf8CharLength(u8(text.front())), text.size()));
        }

        return { result, replaced };
    }

    std::string ImHexStringCodec::decode(std::span<const u8> bytes, std::string_view encoding) const {
        const auto name = resolveEncodingName(encoding);

        // Drop trailing NUL padding, one whole unit at a time.
        const size_t unitSize = (name == "UTF-32LE" || name == "UTF-32BE" || name == "UTF-32") ? 4
                               : (name == "UTF-16LE" || name == "UTF-16BE" || name == "UTF-16") ? 2
                               : 1;
        while (bytes.size() >= unitSize && std::ranges::all_of(bytes.last(unitSize), [](u8 b) { return b == 0x00; }))
            bytes = bytes.first(bytes.size() - unitSize);

        if (name == "UTF-8")
            return decodeUtf8Lossy(bytes);
        if (name == "UTF-16LE")
            return decodeUtf16Lossy(bytes, std::endian::little);
        if (name == "UTF-16BE")
            return decodeUtf16Lossy(bytes, std::endian::big);
        if (name == "UTF-32LE")
            return decodeUtf32Lossy(bytes, std::endian::little);
        if (name == "UTF-32BE")
            return decodeUtf32Lossy(bytes, std::endian::big);
        // No BOM: the Unicode Standard defaults to big-endian.
        if (name == "UTF-16")
            return decodeUtf16Lossy(bytes, std::endian::big);
        if (name == "UTF-32")
            return decodeUtf32Lossy(bytes, std::endian::big);

        // An unknown name decodes as empty. decode() must never throw.
        const auto *table = getEncodingByName(name);
        if (table == nullptr)
            return "";

        return table->decodeAll(bytes);
    }

    std::vector<u8> ImHexStringCodec::encode(std::string_view text, std::string_view encoding) const {
        const auto name = resolveEncodingName(encoding);

        if (name == "UTF-8")
            return encodeUtf8(text);
        if (name == "UTF-16LE")
            return encodeUtf16(text, std::endian::little);
        if (name == "UTF-16BE")
            return encodeUtf16(text, std::endian::big);
        if (name == "UTF-32LE")
            return encodeUtf32(text, std::endian::little);
        if (name == "UTF-32BE")
            return encodeUtf32(text, std::endian::big);
        if (name == "UTF-16")
            return encodeUtf16(text, std::endian::big);
        if (name == "UTF-32")
            return encodeUtf32(text, std::endian::big);

        const auto *table = getEncodingByName(name);
        if (table == nullptr)
            throw std::runtime_error(fmt::format("unknown encoding '{}'", name));

        auto result = table->encodeAll(text);
        if (!result.has_value())
            throw std::runtime_error(fmt::format("text has no byte value in encoding '{}'", name));

        return *result;
    }

}
