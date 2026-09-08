#include <hex/helpers/string_codec.hpp>
#include <hex/helpers/encoding_file.hpp>
#include <hex/api/imhex_api/hex_editor.hpp>

#include <algorithm>
#include <span>

namespace hex {

    namespace {

        // Resolves to `encoding` if non-empty, else the document's declared encoding, else UTF-8.
        std::string resolveEncodingName(std::string_view encoding) {
            if (!encoding.empty())
                return std::string(encoding);

            if (const auto declaredEncoding = ImHexApi::HexEditor::getEncodingName(); declaredEncoding.has_value())
                return *declaredEncoding;

            return "UTF-8";
        }

        // Encodes `text` under the algorithmic encoding `name` names. Returns
        // std::nullopt for a name isAlgorithmicEncodingName() rejects, and for
        // a `text` that is not valid UTF-8.
        std::optional<std::vector<u8>> encodeAlgorithmicText(std::string_view name, std::string_view text) {
            // Checked once here, for every target. encodeUtf8() alone would copy
            // a malformed sequence straight through: its input is meant to be
            // valid UTF-8 already, so it has nothing to check against.
            if (!isValidUtf8(text))
                return std::nullopt;

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

            return std::nullopt;
        }

        // Replaces each malformed or truncated UTF-8 sequence in `text` with U+FFFD,
        // so the result is always well-formed UTF-8.
        std::string sanitizeUtf8(std::string_view text) {
            std::string result;

            while (!text.empty()) {
                const auto step = decodeUtf8Bounded(std::span(reinterpret_cast<const u8 *>(text.data()), text.size()), 1);
                if (step.bytesConsumed == 0) {
                    result += "\xEF\xBF\xBD";
                    text = text.substr(1);
                    continue;
                }

                result += text.substr(0, step.bytesConsumed);
                text = text.substr(step.bytesConsumed);
            }

            return result;
        }

    }

    pl::core::DecodeResult ImHexStringCodec::decode(std::span<const u8> bytes, std::string_view encoding, std::optional<size_t> maxCodepoints) const {
        const auto name = resolveEncodingName(encoding);

        if (const auto algorithmic = decodeAlgorithmicTextBounded(name, bytes, maxCodepoints); algorithmic.has_value())
            return *algorithmic;

        const auto *table = getEncodingByName(name);
        if (table == nullptr) {
            pl::core::DecodeResult result;
            result.stopReason = pl::core::DecodeStop::MalformedBytes;
            return result;
        }

        return table->decodeBounded(bytes, maxCodepoints);
    }

    std::optional<std::vector<u8>> ImHexStringCodec::encode(std::string_view text, std::string_view encoding) const {
        const auto name = resolveEncodingName(encoding);

        // An algorithmic name is answered by its own encoder alone. Falling
        // through on failure would find a .tbl table under the same name -
        // "UTF-8" resolves to encodings/utf8.tbl - and quietly encode with it
        // instead of reporting that the text does not fit.
        if (isAlgorithmicEncodingName(name))
            return encodeAlgorithmicText(name, text);

        const auto *table = getEncodingByName(name);
        if (table == nullptr)
            return std::nullopt;

        return table->encodeAll(text);
    }

    std::vector<u8> ImHexStringCodec::encodeLossy(std::string_view text, std::string_view encoding) const {
        const auto name = resolveEncodingName(encoding);

        // Every path below needs well-formed UTF-8, so sanitize once up front
        // rather than per encoder.
        const std::string sanitized = sanitizeUtf8(text);

        // Sanitized text is always valid UTF-8, so an algorithmic encoding
        // always has an answer for it.
        if (isAlgorithmicEncodingName(name))
            return encodeAlgorithmicText(name, sanitized).value_or(std::vector<u8>{});

        const auto *table = getEncodingByName(name);
        if (table == nullptr)
            return {};

        // U+FFFD when this encoding has a byte for it, '?' otherwise. Most single byte
        // codepages have no byte for U+FFFD at all.
        const auto replacement = table->getBytesFor("\xEF\xBF\xBD").value_or(table->getBytesFor("?").value_or(std::pair<std::vector<u8>, size_t>{}));

        // Builds bytes directly, one matched or replaced code point at a time, instead
        // of through encodeAll(): encodeAll() refuses an ambiguous table outright (see
        // canEncode()), but a lossy write already accepts approximation, so ambiguity
        // should not block it.
        std::string_view remaining = sanitized;

        std::vector<u8> result;
        while (!remaining.empty()) {
            if (auto match = table->getBytesFor(remaining); match.has_value()) {
                result.insert(result.end(), match->first.begin(), match->first.end());
                remaining = remaining.substr(match->second);
                continue;
            }

            result.insert(result.end(), replacement.first.begin(), replacement.first.end());

            const auto step = decodeUtf8Bounded(std::span(reinterpret_cast<const u8 *>(remaining.data()), remaining.size()), 1);
            remaining = remaining.substr(std::min(step.bytesConsumed, remaining.size()));
        }

        return result;
    }

}
