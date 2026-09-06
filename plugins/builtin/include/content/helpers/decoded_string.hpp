#pragma once

#include <hex.hpp>
#include <hex/api/localization_manager.hpp>
#include <hex/helpers/fmt.hpp>
#include <hex/helpers/encoding_file.hpp>

#include <ui/control_byte_picture.hpp>

#include <pl/core/string_encode_decode.hpp>

#include <wolv/utils/string.hpp>

#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace hex::plugin::builtin {

    using hex::ui::NulPicture;
    using hex::ui::nulToPicture;
    using hex::ui::pictureToNul;

    // Matches PatternString::formatDisplayValue()'s own budget, so a Data Inspector
    // string row and a pattern's own hover tooltip / tree view agree on how much of
    // a string to decode and where to mark it truncated.
    constexpr static auto DisplayBudget = 0x7F;

    // A string row's click-select size never ends inside a code point cell, the
    // same as a single-code-point row's own size. Extends `targetSize` (the
    // selection as the user made it) forward through whole code points, stopping
    // as soon as it covers at least `targetSize` bytes - past `targetSize` when
    // the code point straddling it needs more than what was selected.
    inline size_t extendToWholeCodePoints(const std::vector<u8> &buffer, size_t targetSize,
            const std::function<pl::core::DecodeResult(std::span<const u8>)> &decodeOne) {
        size_t bytesConsumed = 0;
        while (bytesConsumed < targetSize && bytesConsumed < buffer.size()) {
            const auto result = decodeOne(std::span(buffer).subspan(bytesConsumed));
            if (result.codepointCount == 0)
                break;

            bytesConsumed += result.bytesConsumed;
        }

        return bytesConsumed;
    }

    // Decodes forward one code point at a time from `buffer`'s start, covering at
    // least `targetSize` bytes - past `targetSize` when the code point straddling
    // it needs more than what was selected, the same rule extendToWholeCodePoints()
    // follows for click-select - and up to `codepointLimit` code points for
    // display. A short or malformed code point before `targetSize` is covered
    // makes the whole result invalid (stopReason MalformedBytes); one after is
    // just where the display window ends, the same as PatternString's own budget
    // cutoff.
    inline pl::core::DecodeResult decodeThroughSelection(const std::vector<u8> &buffer, size_t targetSize, size_t codepointLimit,
            const std::function<pl::core::DecodeResult(std::span<const u8>)> &decodeOne) {
        pl::core::DecodeResult total;

        while (total.codepointCount < codepointLimit && total.bytesConsumed < buffer.size()) {
            const auto step = decodeOne(std::span(buffer).subspan(total.bytesConsumed));
            if (step.codepointCount == 0) {
                total.stopReason = (total.bytesConsumed < targetSize) ? pl::core::DecodeStop::MalformedBytes : step.stopReason;
                return total;
            }

            total.text += step.text;
            total.bytesConsumed += step.bytesConsumed;
            total.codepointCount += step.codepointCount;

            if (total.bytesConsumed >= targetSize)
                break;
        }

        return total;
    }

    // Formats the one code point at `buffer`'s start under the named algorithmic
    // encoding, as the character itself and its U+ notation. Returns std::nullopt
    // when those bytes are not one whole, valid code point - a malformed sequence,
    // a lone surrogate, or a code point the buffer cuts off. A row shows
    // "hex.builtin.inspector.invalid" for all three.
    inline std::optional<std::string> formatCodePoint(std::string_view encodingName, std::span<const u8> buffer) {
        const auto decoded = decodeAlgorithmicTextBounded(encodingName, buffer, 1);
        if (!decoded.has_value() || decoded->codepointCount == 0)
            return std::nullopt;

        const auto codepoints = wolv::util::utf8ToUtf32(decoded->text);
        if (!codepoints.has_value() || codepoints->empty())
            return std::nullopt;

        const char32_t codepoint = codepoints->front();
        return fmt::format("'{0}' (U+{1:04X})", escapeCodepoint(codepoint), u32(codepoint));
    }

    // How many bytes the code point at `buffer`'s start uses. Falls back to
    // `codeUnitSize` when those bytes decode to nothing, so that clicking a
    // malformed row still selects one whole code unit instead of nothing.
    inline size_t codePointSize(std::string_view encodingName, std::span<const u8> buffer, size_t codeUnitSize) {
        const auto decoded = decodeAlgorithmicTextBounded(encodingName, buffer, 1);
        if (!decoded.has_value() || decoded->bytesConsumed == 0)
            return codeUnitSize;

        return decoded->bytesConsumed;
    }

    // Formats a decoded string row the same way PatternString::formatDisplayValue()
    // does: the C++ string literal prefix the row's encoding uses (none for UTF-8,
    // "u" for UTF-16, "U" for UTF-32), the quoted decoded text, and a truncation
    // mark when the selection holds more bytes than the display budget decoded.
    inline std::string formatDecodedString(std::string_view literalPrefix, const pl::core::DecodeResult &decoded, size_t selectionSize) {
        // decoded.text is built one codepoint at a time and is always
        // valid UTF-8. escapeControlCharacters() never falls back here.
        const auto escaped = escapeControlCharacters(decoded.text).value_or("hex.builtin.inspector.invalid"_lang.get());

        if (decoded.bytesConsumed < selectionSize)
            return fmt::format("{0}\"{1}\" {2}", literalPrefix, escaped, "hex.builtin.inspector.truncated"_lang);

        return fmt::format("{0}\"{1}\"", literalPrefix, escaped);
    }

}
