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

        // This works like getEncodingFor(). But it returns std::nullopt when the
        // encoding has no entry for these bytes, instead of returning ".".
        [[nodiscard]] std::optional<std::pair<std::string_view, size_t>> lookup(std::span<const u8> buffer) const;

        [[nodiscard]] std::pair<std::string_view, size_t> getEncodingFor(std::span<const u8> buffer) const;
        [[nodiscard]] u64 getEncodingLengthFor(std::span<u8> buffer) const;
        [[nodiscard]] u64 getShortestSequence() const { return m_shortestSequence; }
        [[nodiscard]] u64 getLongestSequence()  const { return m_longestSequence;  }
        [[nodiscard]] std::string decodeAll(std::span<const u8> buffer) const;

        // Returns std::nullopt if `sequence` does not start with a known encoded value.
        [[nodiscard]] std::optional<std::pair<std::vector<u8>, size_t>> getBytesFor(std::string_view sequence) const;

        // False when the encoding is ambiguous: one decoded value maps to more than one byte
        // sequence, or one decoded value is a prefix of another.
        [[nodiscard]] bool canEncode() const { return !m_ambiguousEncoding; }

        // Returns std::nullopt in two cases. The encoding is ambiguous; see canEncode().
        // Or `sequence` has a character with no byte value in this encoding.
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

    // The character set a file's text is read with: one byte to at most one character, for all
    // 256 byte values, no state, no multi-byte sequences. Lets a text view start decoding at any
    // scroll position and still give every byte its own cell.
    class Codepage {
    public:
        // Plain 7-bit ASCII. A file's text reads this way when nothing else is declared.
        static const Codepage& ascii();

        // Returns std::nullopt if `encoding` is not a codepage: one or more of its characters
        // take more than a single byte.
        static std::optional<Codepage> fromEncoding(const EncodingFile &encoding);

        // Empty when the codepage gives `byte` no character of its own - a control code
        // (spelled out as a name, like "NUL" or "CR") or an unmapped byte.
        [[nodiscard]] std::string_view operator[](u8 byte) const { return m_characters[byte]; }

        // Empty for the default ASCII codepage. Callers label that case in their own words.
        [[nodiscard]] const std::string& getName() const { return m_name; }

    private:
        Codepage() = default;

        std::array<std::string, 256> m_characters;
        std::string m_name;
    };

    // Looks up an encoding by its table file's name, without the extension - for example
    // "macintosh" finds encodings/macintosh.tbl. Only files directly inside a configured
    // encodings directory; discards any directory part in `name`. Returns nullptr if no such
    // table exists.
    //
    // Parses each table once and caches it for the process's life. Some tables, like utf8.tbl,
    // have over 100,000 entries.
    const EncodingFile* getEncodingByName(const std::string &name);

    // Decodes `bytes` into text for display on a single UI line. A byte sequence that decodes
    // to exactly one character shows as that character. Everything else escapes the way the
    // pattern language escapes a string, covering control code names like "NUL" and bytes the
    // encoding does not know.
    std::string decodeForDisplay(const EncodingFile &encoding, std::span<const u8> bytes);

    // UTF-8, UTF-16, and UTF-32 decode by a fixed algorithm, not a .tbl table - UTF-32 alone
    // would need over a million four-byte table keys. Returns std::nullopt on a malformed
    // sequence: invalid UTF-8, an unpaired UTF-16 surrogate, or an out-of-range UTF-32 value.
    std::optional<std::string> decodeUtf8(std::span<const u8> bytes);
    std::optional<std::string> decodeUtf16(std::span<const u8> bytes, std::endian endian);
    std::optional<std::string> decodeUtf32(std::span<const u8> bytes, std::endian endian);

    // Real text codecs, not display formatters. An undecodable byte becomes U+FFFD, the standard
    // Unicode replacement character, not an escape sequence. Escaping is a display convention; a
    // real decoded value must not contain backslash sequences a reader did not put there.
    std::string decodeUtf8Lossy(std::span<const u8> bytes);
    std::string decodeUtf16Lossy(std::span<const u8> bytes, std::endian endian);
    std::string decodeUtf32Lossy(std::span<const u8> bytes, std::endian endian);

    // Encodes UTF-8 `text` into the named byte encoding. Every Unicode scalar value fits in
    // UTF-16 or UTF-32, so these two never fail. `encodeUtf8` never fails either - the input is
    // already UTF-8.
    std::vector<u8> encodeUtf8(std::string_view text);
    std::vector<u8> encodeUtf16(std::string_view text, std::endian endian);
    std::vector<u8> encodeUtf32(std::string_view text, std::endian endian);

}
