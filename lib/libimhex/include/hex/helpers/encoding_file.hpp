#pragma once

#include <hex.hpp>

#include <array>
#include <bit>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>
#include <span>

#include <wolv/io/fs.hpp>

#include <pl/core/string_encode_decode.hpp>

namespace hex {

    namespace impl {
        void appendEncodingLineStartAddress(std::vector<u64> &lineStartAddresses, size_t line, u64 nextLineStartAddress);
    }

    class EncodingFile {
    public:
        enum class Type
        {
            Thingy
        };

        EncodingFile();
        EncodingFile(const EncodingFile &other);
        EncodingFile(EncodingFile &&other) noexcept;
        EncodingFile(Type type, const std::fs::path &path);
        EncodingFile(Type type, const std::string &content);

        EncodingFile& operator=(const EncodingFile &other);
        EncodingFile& operator=(EncodingFile &&other) noexcept;

        // Works like getEncodingFor(). Returns std::nullopt when the encoding has
        // no entry for these bytes.
        [[nodiscard]] std::optional<std::pair<std::string_view, size_t>> lookup(std::span<const u8> buffer) const;

        [[nodiscard]] std::pair<std::string_view, size_t> getEncodingFor(std::span<const u8> buffer) const;
        [[nodiscard]] u64 getEncodingLengthFor(std::span<u8> buffer) const;
        [[nodiscard]] u64 getShortestSequence() const { return m_shortestSequence; }
        [[nodiscard]] u64 getLongestSequence()  const { return m_longestSequence;  }
        [[nodiscard]] std::string decodeAll(std::span<const u8> buffer) const;

        // Whether every byte in `buffer` decodes to a known value in this
        // encoding, with no leftover unmapped byte. False for a buffer
        // getEncodingFor() would otherwise paper over with a "." placeholder.
        [[nodiscard]] bool isFullyMapped(std::span<const u8> buffer) const;

        // Decodes `buffer` one entry at a time, stopping at the first byte
        // sequence with no entry, at the end of `buffer`, or after maxCodepoints
        // entries - whichever comes first. Unlike isFullyMapped() + decodeAll(),
        // this tells apart a buffer that is simply too short for even the
        // shortest mapped sequence (DecodeStop::EndOfInput) from one with a byte
        // sequence this encoding does not know (DecodeStop::MalformedBytes).
        [[nodiscard]] pl::core::DecodeResult decodeBounded(std::span<const u8> buffer, std::optional<size_t> maxCodepoints = std::nullopt) const;

        // Returns std::nullopt if `sequence` does not start with a known encoded value.
        [[nodiscard]] std::optional<std::pair<std::vector<u8>, size_t>> getBytesFor(std::string_view sequence) const;

        // False when one decoded value maps to more than one byte sequence, or one
        // decoded value is a prefix of another.
        [[nodiscard]] bool canEncode() const { return !m_ambiguousEncoding; }

        // Returns std::nullopt when the encoding is ambiguous (see canEncode()), or
        // when `sequence` has a character with no byte value in this encoding.
        [[nodiscard]] std::optional<std::vector<u8>> encodeAll(std::string_view sequence) const;

        [[nodiscard]] bool valid() const { return m_valid; }

        [[nodiscard]] const std::string& getTableContent() const { return m_tableContent; }

        [[nodiscard]] const std::string& getName() const { return m_name; }

    private:
        void parse(const std::string &content);

        bool m_valid = false;

        std::string m_name;
        std::string m_tableContent;
        std::unique_ptr<std::map<size_t, std::map<std::vector<u8>, std::string>>> m_mapping;
        std::unique_ptr<std::map<size_t, std::map<std::string, std::vector<u8>, std::less<>>>> m_reverseMapping;

        u64 m_shortestSequence = std::numeric_limits<u64>::max();
        u64 m_longestSequence  = std::numeric_limits<u64>::min();

        bool m_ambiguousEncoding = false;
    };

    // A single-byte character set: one byte maps to at most one character, for
    // all 256 byte values. Has no state and no multi-byte sequences. A text view
    // can start decoding at any scroll position and give every byte its own cell.
    class Codepage {
    public:
        // Plain 7-bit ASCII. A file's text reads this way when nothing else is declared.
        static const Codepage& ascii();

        // Returns std::nullopt when `encoding` is not a codepage: one or more of its
        // characters take more than one byte.
        static std::optional<Codepage> fromEncoding(const EncodingFile &encoding);

        // Empty when the codepage gives `byte` no character of its own. This
        // covers a control code (spelled out as a name, like "NUL" or "CR") and
        // an unmapped byte.
        [[nodiscard]] std::string_view operator[](u8 byte) const { return m_characters[byte]; }

        // Empty for the default ASCII codepage.
        [[nodiscard]] const std::string& getName() const { return m_name; }

    private:
        Codepage() = default;

        std::array<std::string, 256> m_characters;
        std::string m_name;
    };

    // Looks up an encoding by its table file's name, without the extension. For
    // example, "macintosh" finds encodings/macintosh.tbl. Looks only in the
    // configured encodings directory and discards any directory part in `name`.
    // Returns nullptr when no such table exists.
    //
    // Parses each table once and caches it for the life of the process.
    const EncodingFile* getEncodingByName(const std::string &name);

    // Whether `text` is exactly one whole, valid UTF-8 code point: something
    // that fits in a single cell.
    bool isSingleCharacter(std::string_view text);

    // Whether `byte` is a standard-ASCII control code. It has no glyph.
    bool isControlCode(u8 byte);

    // Escapes one decoded codepoint for display. A non-printable ASCII
    // byte becomes \xNN. A codepoint above ASCII with no glyph becomes
    // \uNNNN, or \UNNNNNNNN past U+FFFF. All else passes through.
    std::string escapeCodepoint(char32_t codepoint);

    // Escapes decoded UTF-8 text with escapeCodepoint(). A lone trailing
    // NUL becomes \0, not \x00. Returns std::nullopt on invalid UTF-8: a
    // bad byte cannot round-trip as an escape. Show "Invalid" instead.
    std::optional<std::string> escapeControlCharacters(std::string_view text);

    // Whether `text` is well-formed UTF-8 all the way through.
    bool isValidUtf8(std::string_view text);

    // Encodes UTF-8 `text` into the named byte encoding. encodeUtf8() never
    // fails; the input is already UTF-8. encodeUtf16()/encodeUtf32() return
    // std::nullopt when `text` is not valid UTF-8.
    std::vector<u8> encodeUtf8(std::string_view text);
    std::optional<std::vector<u8>> encodeUtf16(std::string_view text, std::endian endian);
    std::optional<std::vector<u8>> encodeUtf32(std::string_view text, std::endian endian);

    // Whether `name` names an algorithmic Unicode encoding: "UTF-8",
    // "UTF-16LE", "UTF-16BE", "UTF-32LE" or "UTF-32BE", spelled the way ICU's
    // converter names spell them. A name this rejects is a .tbl table's name;
    // resolve it through getEncodingByName() instead.
    //
    // A bare "UTF-16"/"UTF-32" is deliberately absent. Those name the
    // BOM-carrying encoding schemes of the Unicode Standard, and nothing here
    // reads or writes a BOM, so accepting them would silently guess the byte
    // order. Say which end you mean.
    bool isAlgorithmicEncodingName(std::string_view name);

    // Decodes UTF-8, UTF-16, or UTF-32 by a fixed algorithm, not a .tbl table.
    // Decodes incrementally: stops at the first malformed byte, at the end of
    // `bytes`, or after maxCodepoints code points - whichever comes first. A
    // code point cut off by the end of `bytes` stops at DecodeStop::EndOfInput,
    // not DecodeStop::MalformedBytes.
    pl::core::DecodeResult decodeUtf8Bounded(std::span<const u8> bytes, std::optional<size_t> maxCodepoints = std::nullopt);
    pl::core::DecodeResult decodeUtf16Bounded(std::span<const u8> bytes, std::endian endian, std::optional<size_t> maxCodepoints = std::nullopt);
    pl::core::DecodeResult decodeUtf32Bounded(std::span<const u8> bytes, std::endian endian, std::optional<size_t> maxCodepoints = std::nullopt);

    // Dispatches to the decoder isAlgorithmicEncodingName() accepts `name` for.
    // Returns std::nullopt for every other name.
    std::optional<pl::core::DecodeResult> decodeAlgorithmicTextBounded(std::string_view name, std::span<const u8> bytes, std::optional<size_t> maxCodepoints = std::nullopt);

}
