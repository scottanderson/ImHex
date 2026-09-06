#include <hex/test/tests.hpp>

#include <content/helpers/decoded_string.hpp>

#include <span>
#include <vector>

using namespace hex::plugin::builtin;

namespace {

    std::vector<u8> bytesOf(std::string_view text) {
        return { text.begin(), text.end() };
    }

    // One UTF-8 code point at a time, the same shape ImHexStringCodec::decode()
    // gives these helpers at run time.
    pl::core::DecodeResult decodeOneUtf8(std::span<const u8> bytes) {
        return hex::decodeUtf8Bounded(bytes, 1);
    }

}

TEST_SEQUENCE("DecodeThroughSelectionStopsAtTheSelection") {
    // The buffer a Data Inspector row receives is read from the selection's
    // start for up to maxSize bytes, whatever the selection length. Decoding to
    // the end of it would read unrelated, unselected file bytes into the row.
    const auto buffer = bytesOf("ab" "UNSELECTED");

    const auto decoded = decodeThroughSelection(buffer, 2, DisplayBudget, decodeOneUtf8);
    TEST_ASSERT(decoded.text == "ab");
    TEST_ASSERT(decoded.bytesConsumed == 2);

    // A one byte selection still stops after one byte.
    const auto single = decodeThroughSelection(buffer, 1, DisplayBudget, decodeOneUtf8);
    TEST_ASSERT(single.text == "a");
    TEST_ASSERT(single.bytesConsumed == 1);

    TEST_SUCCESS();
};

TEST_SEQUENCE("DecodeThroughSelectionFinishesAStraddlingCodePoint") {
    // U+00E9 is two bytes. A selection covering only its first byte still
    // decodes the whole character rather than reporting a broken one.
    const auto buffer = bytesOf("\xC3\xA9" "tail");

    const auto decoded = decodeThroughSelection(buffer, 1, DisplayBudget, decodeOneUtf8);
    TEST_ASSERT(decoded.text == "\xC3\xA9");
    TEST_ASSERT(decoded.bytesConsumed == 2);
    TEST_ASSERT(decoded.stopReason != pl::core::DecodeStop::MalformedBytes);

    TEST_SUCCESS();
};

TEST_SEQUENCE("DecodeThroughSelectionReportsMalformedBytes") {
    // A byte the encoding cannot decode, inside the selection, invalidates the
    // whole row.
    const auto broken = bytesOf("a\xFF" "bc");
    const auto insideSelection = decodeThroughSelection(broken, 4, DisplayBudget, decodeOneUtf8);
    TEST_ASSERT(insideSelection.stopReason == pl::core::DecodeStop::MalformedBytes);

    // The same byte past the selection is just where the display window ends.
    const auto pastSelection = decodeThroughSelection(broken, 1, DisplayBudget, decodeOneUtf8);
    TEST_ASSERT(pastSelection.stopReason != pl::core::DecodeStop::MalformedBytes);
    TEST_ASSERT(pastSelection.text == "a");

    TEST_SUCCESS();
};

TEST_SEQUENCE("DecodeThroughSelectionHonoursTheDisplayBudget") {
    const auto buffer = bytesOf("abcdef");

    const auto decoded = decodeThroughSelection(buffer, 6, 3, decodeOneUtf8);
    TEST_ASSERT(decoded.text == "abc");
    TEST_ASSERT(decoded.codepointCount == 3);

    // Fewer bytes decoded than the selection holds, so the row reads truncated.
    TEST_ASSERT(decoded.bytesConsumed < 6);

    TEST_SUCCESS();
};

TEST_SEQUENCE("ExtendToWholeCodePoints") {
    // A click never selects half a character.
    const auto buffer = bytesOf("a\xC3\xA9" "b");

    TEST_ASSERT(extendToWholeCodePoints(buffer, 1, decodeOneUtf8) == 1);

    // Two bytes lands inside U+00E9, so the size grows to cover it.
    TEST_ASSERT(extendToWholeCodePoints(buffer, 2, decodeOneUtf8) == 3);
    TEST_ASSERT(extendToWholeCodePoints(buffer, 3, decodeOneUtf8) == 3);
    TEST_ASSERT(extendToWholeCodePoints(buffer, 4, decodeOneUtf8) == 4);

    // Asking for more than the buffer holds stops at the buffer.
    TEST_ASSERT(extendToWholeCodePoints(buffer, 99, decodeOneUtf8) == 4);

    TEST_SUCCESS();
};

TEST_SEQUENCE("FormatDecodedStringMarksTruncation") {
    pl::core::DecodeResult decoded;
    decoded.text = "ab";
    decoded.bytesConsumed = 2;

    // The literal prefix is the one the row's encoding uses: none for UTF-8,
    // "u" for UTF-16, "U" for UTF-32.
    TEST_ASSERT(formatDecodedString("", decoded, 2) == "\"ab\"");
    TEST_ASSERT(formatDecodedString("u", decoded, 2) == "u\"ab\"");

    // More bytes selected than got decoded means the display was cut short.
    TEST_ASSERT(formatDecodedString("", decoded, 5).contains("truncated"));

    // Control characters escape rather than breaking the single display line.
    pl::core::DecodeResult withNewline;
    withNewline.text = "a\nb";
    withNewline.bytesConsumed = 3;
    TEST_ASSERT(formatDecodedString("", withNewline, 3) == "\"a\\nb\"");

    TEST_SUCCESS();
};

TEST_SEQUENCE("NulPictureRoundTrip") {
    // ImGui's InputText is NUL-terminated, so a NUL byte needs a stand-in while
    // a value is being edited.
    const std::string withNul("a\0b", 3);

    TEST_ASSERT(nulToPicture(withNul) == "a\xE2\x90\x80" "b");
    TEST_ASSERT(pictureToNul(nulToPicture(withNul)) == withNul);

    // Text with no NUL passes through untouched.
    TEST_ASSERT(nulToPicture("abc") == "abc");
    TEST_ASSERT(pictureToNul("abc") == "abc");

    TEST_SUCCESS();
};

TEST_SEQUENCE("FormatCodePoint") {
    const auto format = [](std::string_view encoding, const std::vector<u8> &bytes) {
        return formatCodePoint(encoding, bytes);
    };

    TEST_ASSERT(format("UTF-8", bytesOf("A")).value() == "'A' (U+0041)");
    TEST_ASSERT(format("UTF-8", { 0xC3, 0xA9 }).value() == "'\xC3\xA9' (U+00E9)");
    TEST_ASSERT(format("UTF-8", { 0xF0, 0x9F, 0x98, 0x80 }).value() == "'\xF0\x9F\x98\x80' (U+1F600)");

    // U+FFFD is an ordinary character, not a decode failure. The row must show
    // it as itself. This is what the old ImTextCharFromUtf8() sentinel could
    // not tell apart.
    TEST_ASSERT(format("UTF-8", { 0xEF, 0xBF, 0xBD }).value() == "'\xEF\xBF\xBD' (U+FFFD)");
    TEST_ASSERT(format("UTF-16LE", { 0xFD, 0xFF }).value() == "'\xEF\xBF\xBD' (U+FFFD)");

    // A code point with no glyph shows as its escape, not as a blank cell.
    TEST_ASSERT(format("UTF-8", { 0xEF, 0xBB, 0xBF }).value() == "'\\uFEFF' (U+FEFF)");
    TEST_ASSERT(format("UTF-8", { 0x0A }).value() == "'\\n' (U+000A)");

    // Byte order is the row's own, not the shared endian toggle.
    TEST_ASSERT(format("UTF-16LE", { 0x41, 0x00 }).value() == "'A' (U+0041)");
    TEST_ASSERT(format("UTF-16BE", { 0x00, 0x41 }).value() == "'A' (U+0041)");
    TEST_ASSERT(format("UTF-32LE", { 0x00, 0xF6, 0x01, 0x00 }).value() == "'\xF0\x9F\x98\x80' (U+1F600)");
    TEST_ASSERT(format("UTF-32BE", { 0x00, 0x01, 0xF6, 0x00 }).value() == "'\xF0\x9F\x98\x80' (U+1F600)");

    // A surrogate pair is one code point.
    TEST_ASSERT(format("UTF-16LE", { 0x3D, 0xD8, 0x00, 0xDE }).value() == "'\xF0\x9F\x98\x80' (U+1F600)");

    // Invalid: a lone surrogate of either half, a high surrogate followed by
    // something that is not a low one, a malformed UTF-8 sequence, and a
    // UTF-32 value outside the codespace.
    TEST_ASSERT(!format("UTF-16LE", { 0x3D, 0xD8 }).has_value());
    TEST_ASSERT(!format("UTF-16LE", { 0x00, 0xDC }).has_value());
    TEST_ASSERT(!format("UTF-16LE", { 0x3D, 0xD8, 0x41, 0x00 }).has_value());
    TEST_ASSERT(!format("UTF-8", { 0x80 }).has_value());
    TEST_ASSERT(!format("UTF-8", { 0xC0, 0xAF }).has_value());
    TEST_ASSERT(!format("UTF-32LE", { 0x00, 0x00, 0x11, 0x00 }).has_value());

    // A code point the buffer cuts off short is invalid too: there is no whole
    // character to show yet.
    TEST_ASSERT(!format("UTF-8", { 0xC3 }).has_value());
    TEST_ASSERT(!format("UTF-16LE", { 0x41 }).has_value());

    // A name no algorithmic decoder handles.
    TEST_ASSERT(!format("shiftjis", bytesOf("A")).has_value());

    TEST_SUCCESS();
};

TEST_SEQUENCE("CodePointSize") {
    const auto sizeOf = [](std::string_view encoding, const std::vector<u8> &bytes, size_t codeUnitSize) {
        return codePointSize(encoding, bytes, codeUnitSize);
    };

    // How many bytes a click on the row should select.
    TEST_ASSERT(sizeOf("UTF-8", bytesOf("A"), 1) == 1);
    TEST_ASSERT(sizeOf("UTF-8", { 0xC3, 0xA9 }, 1) == 2);
    TEST_ASSERT(sizeOf("UTF-8", { 0xF0, 0x9F, 0x98, 0x80 }, 1) == 4);

    // A surrogate pair takes both code units; an unpaired unit takes one.
    TEST_ASSERT(sizeOf("UTF-16LE", { 0x3D, 0xD8, 0x00, 0xDE }, 2) == 4);
    TEST_ASSERT(sizeOf("UTF-16LE", { 0x41, 0x00, 0x42, 0x00 }, 2) == 2);

    // Bytes that decode to nothing still select one whole code unit, never 0.
    // Selecting 0 bytes on click would clear the selection instead of moving it.
    TEST_ASSERT(sizeOf("UTF-8", { 0xC3 }, 1) == 1);
    TEST_ASSERT(sizeOf("UTF-8", { 0x80 }, 1) == 1);
    TEST_ASSERT(sizeOf("UTF-16LE", { 0x00, 0xDC }, 2) == 2);
    TEST_ASSERT(sizeOf("UTF-16LE", { 0x3D, 0xD8 }, 2) == 2);
    TEST_ASSERT(sizeOf("UTF-32LE", { 0x00, 0x00, 0x11, 0x00 }, 4) == 4);
    TEST_ASSERT(sizeOf("shiftjis", bytesOf("A"), 1) == 1);

    TEST_SUCCESS();
};
