#include <hex/helpers/encoding_file.hpp>

#include <hex/helpers/default_paths.hpp>
#include <hex/helpers/fmt.hpp>
#include <hex/helpers/utils.hpp>

#include <algorithm>
#include <array>
#include <filesystem>
#include <mutex>
#include <ranges>
#include <wolv/io/file.hpp>
#include <wolv/utils/string.hpp>

namespace hex {

    namespace {

        // Bytes 0x00-0x7F always mean standard ASCII: 0x00-0x1F and 0x7F get control-code names
        // like "NUL" and "DEL"; 0x20-0x7E get their own character. encodings/macintosh.tbl is
        // one table that follows this rule. A table that redefines this range is rejected.
        constexpr static std::array<std::string_view, 128> StandardAsciiRange = {
            "NUL", "SOH", "STX", "ETX", "EOT", "ENQ", "ACK", "BEL",
            "BS",  "TAB", "LF",  "VT",  "FF",  "CR",  "SO",  "SI",
            "DLE", "DC1", "DC2", "DC3", "DC4", "NAK", "SYN", "ETB",
            "CAN", "EM",  "SUB", "ESC", "FS",  "GS",  "RS",  "US",
            " ", "!", "\"", "#", "$", "%", "&", "'",
            "(", ")", "*",  "+", ",", "-", ".", "/",
            "0", "1", "2",  "3", "4", "5", "6", "7",
            "8", "9", ":",  ";", "<", "=", ">", "?",
            "@", "A", "B",  "C", "D", "E", "F", "G",
            "H", "I", "J",  "K", "L", "M", "N", "O",
            "P", "Q", "R",  "S", "T", "U", "V", "W",
            "X", "Y", "Z",  "[", "\\", "]", "^", "_",
            "`", "a", "b",  "c", "d", "e", "f", "g",
            "h", "i", "j",  "k", "l", "m", "n", "o",
            "p", "q", "r",  "s", "t", "u", "v", "w",
            "x", "y", "z",  "{", "|", "}", "~", "DEL"
        };

        // Returns the number of bytes at the start of `text` that form one full
        // UTF-8 character. Returns 0 if `text` does not start with one.
        size_t utf8CharacterLength(std::string_view text) {
            if (text.empty())
                return 0;

            const auto leadByte = u8(text[0]);
            size_t length;
            if ((leadByte & 0x80) == 0x00)      length = 1;
            else if ((leadByte & 0xE0) == 0xC0) length = 2;
            else if ((leadByte & 0xF0) == 0xE0) length = 3;
            else if ((leadByte & 0xF8) == 0xF0) length = 4;
            else return 0;

            if (length > text.size())
                return 0;

            for (size_t i = 1; i < length; i += 1) {
                if ((u8(text[i]) & 0xC0) != 0x80)
                    return 0;
            }

            return length;
        }

        // Maps a standard encoding name - an IANA name, or the name a pattern author reaches
        // for - to the ImHex table file that implements it, since ImHex names its tables after
        // their purpose rather than the encoding. A fallback: remove an entry once its file is
        // renamed to the standard name in ImHex-Patterns.
        constexpr static auto EncodingNameAliases = std::to_array<std::pair<std::string_view, std::string_view>>({
            { "us-ascii",     "ascii"                  },
            { "utf-8",        "utf8"                   },

            { "cp437",        "ascii_oem"              },
            { "ibm437",       "ascii_oem"              },
            { "cp1252",       "ascii_ansi"             },
            { "windows-1252", "ascii_ansi"             },

            { "iso-8859-2",   "eastern_europe_iso"     },
            { "windows-1250", "eastern_europe_windows" },
            { "iso-8859-5",   "cyrillic_iso"           },
            { "windows-1251", "cyrillic_windows"       },
            { "cp866",        "cyrillic_cp866"         },
            { "ibm866",       "cyrillic_cp866"         },
            { "koi8-r",       "cyrillic_koi8_r"        },
            { "koi8-u",       "cyrillic_koi8_u"        },
            { "iso-8859-6",   "arabic_iso"             },
            { "windows-1256", "arabic_windows"         },
            { "iso-8859-7",   "greek_iso"              },
            { "windows-1253", "greek_windows"          },
            { "iso-8859-8",   "hebrew_iso"             },
            { "windows-1255", "hebrew_windows"         },
            { "iso-8859-9",   "turkish_iso"            },
            { "windows-1254", "turkish_windows"        },
            { "iso-8859-13",  "baltic_iso"             },
            { "windows-1257", "baltic_windows"         },
            { "windows-874",  "thai"                   },
            { "windows-1258", "vietnamese"             },
            { "iso-6937",     "iso_6937"               },

            { "cp037",        "ebcdic"                 },
            { "ibm037",       "ebcdic"                 },

            { "mac",          "macintosh"              },
            { "x-mac-roman",  "macintosh"              },

            { "shift_jis",    "shiftjis"               },
            { "shift-jis",    "shiftjis"               },
            { "sjis",         "shiftjis"               },
            { "cp932",        "ms932"                  },
            { "windows-31j",  "ms932"                  },
            { "euc-jp",       "euc_jp"                 },
            { "euc-kr",       "euc_kr"                 },
            { "jis_x0201",    "jis_x_0201"             },
        });

        // Returns the path of the encodings/<stem>.tbl file, if there is one.
        std::optional<std::fs::path> findEncodingFile(std::string_view stem) {
            // Discards any directory part (e.g. "../../../etc/passwd"): a pattern script can
            // reach this code without the sandbox prompt hex::file::open() requires, so this
            // must never read an arbitrary file from disk.
            const auto fileName = std::fs::path(stem).filename().string() + ".tbl";

            for (const auto &basePath : paths::Encodings.read()) {
                auto path = basePath / fileName;
                if (std::fs::is_regular_file(path))
                    return path;
            }

            return std::nullopt;
        }

        // Whether `text` is exactly one character: something that fits in a single cell.
        bool isSingleCharacter(std::string_view text) {
            return !text.empty() && utf8CharacterLength(text) == text.size();
        }

        // Escapes a byte with no glyph and no Unicode text to fall back on: a literal backslash,
        // or a control code a table names instead of drawing, like "NUL" or "CR". Both are
        // ASCII, where a byte's value equals its Unicode codepoint, so \u here is exact.
        void appendEscapedByte(std::string &result, u8 byte) {
            switch (byte) {
                case '\\': result += "\\\\"; break;
                case '\a': result += "\\a";  break;
                case '\b': result += "\\b";  break;
                case '\f': result += "\\f";  break;
                case '\n': result += "\\n";  break;
                case '\r': result += "\\r";  break;
                case '\t': result += "\\t";  break;
                case '\v': result += "\\v";  break;
                default:   result += fmt::format("\\u{:04X}", byte); break;
            }
        }

        // Decodes the one UTF-8 character that `text` starts with. Returns its
        // codepoint and byte length. Returns length 0 if `text` does not start
        // with a complete, valid UTF-8 sequence.
        std::pair<char32_t, size_t> utf8Decode(std::string_view text) {
            const auto length = utf8CharacterLength(text);
            if (length == 0)
                return { 0, 0 };

            const auto leadByte = u8(text[0]);
            char32_t codepoint = [&] {
                switch (length) {
                    case 1:  return leadByte & 0x7F;
                    case 2:  return leadByte & 0x1F;
                    case 3:  return leadByte & 0x0F;
                    default: return leadByte & 0x07;
                }
            }();

            for (size_t i = 1; i < length; i += 1)
                codepoint = (codepoint << 6) | (u8(text[i]) & 0x3F);

            return { codepoint, length };
        }

        // Escapes `text` as \u or \U sequences, one per decoded codepoint - not per source byte.
        // A multi-byte table key does not map to its decoded text byte-for-byte, so a
        // multi-codepoint character like a base letter plus a combining accent escapes as two
        // sequences.
        void appendUnicodeCodepoints(std::string &result, std::string_view text) {
            while (!text.empty()) {
                const auto [codepoint, length] = utf8Decode(text);
                if (length == 0) {
                    // Malformed UTF-8 inside the table itself; falls back to the raw byte.
                    result += fmt::format("\\u{:04X}", u8(text[0]));
                    text = text.substr(1);
                    continue;
                }

                if (codepoint > 0xFFFF)
                    result += fmt::format("\\U{:08X}", static_cast<u32>(codepoint));
                else
                    result += fmt::format("\\u{:04X}", static_cast<u32>(codepoint));

                text = text.substr(length);
            }
        }

        // Whether `text` has a byte outside the ASCII range - real decoded Unicode text, as
        // opposed to a plain-ASCII control code name a table spells out, like "NUL".
        bool hasNonAsciiByte(std::string_view text) {
            return std::ranges::any_of(text, [](char c) { return u8(c) >= 0x80; });
        }

    }

    namespace impl {

        void appendEncodingLineStartAddress(std::vector<u64> &lineStartAddresses, size_t line, u64 nextLineStartAddress) {
            if (line + 1 == lineStartAddresses.size())
                lineStartAddresses.push_back(nextLineStartAddress);
        }

    }

    EncodingFile::EncodingFile() :
        m_mapping(std::make_unique<std::map<size_t, std::map<std::vector<u8>, std::string>>>()),
        m_reverseMapping(std::make_unique<std::map<size_t, std::map<std::string, std::vector<u8>, std::less<>>>>()) {

    }

    EncodingFile::EncodingFile(const hex::EncodingFile &other) {
        m_mapping = std::make_unique<std::map<size_t, std::map<std::vector<u8>, std::string>>>(*other.m_mapping);
        m_reverseMapping = std::make_unique<std::map<size_t, std::map<std::string, std::vector<u8>, std::less<>>>>(*other.m_reverseMapping);
        m_tableContent = other.m_tableContent;
        m_longestSequence = other.m_longestSequence;
        m_shortestSequence = other.m_shortestSequence;
        m_ambiguousEncoding = other.m_ambiguousEncoding;
        m_valid = other.m_valid;
        m_name = other.m_name;
    }

    EncodingFile::EncodingFile(EncodingFile &&other) noexcept {
        m_mapping = std::move(other.m_mapping);
        m_reverseMapping = std::move(other.m_reverseMapping);
        m_tableContent = std::move(other.m_tableContent);
        m_longestSequence = other.m_longestSequence;
        m_shortestSequence = other.m_shortestSequence;
        m_ambiguousEncoding = other.m_ambiguousEncoding;
        m_valid = other.m_valid;
        m_name = std::move(other.m_name);
    }

    EncodingFile::EncodingFile(Type type, const std::fs::path &path) : EncodingFile() {
        auto file = wolv::io::File(path, wolv::io::File::Mode::Read);
        switch (type) {
            case Type::Thingy:
                parse(file.readString());
                break;
            default:
                return;
        }

        {
            m_name = path.stem().string();
            m_name = wolv::util::replaceStrings(m_name, "_", " ");

            if (!m_name.empty())
                m_name[0] = std::toupper(m_name[0]);
        }

        m_valid = true;
    }

    EncodingFile::EncodingFile(Type type, const std::string &content) : EncodingFile() {
        switch (type) {
            case Type::Thingy:
                parse(content);
                break;
            default:
                return;
        }

        m_name = "Unknown";
        m_valid = true;
    }


    EncodingFile &EncodingFile::operator=(const hex::EncodingFile &other) {
        if(this == &other) {
            return *this;
        }
        m_mapping = std::make_unique<std::map<size_t, std::map<std::vector<u8>, std::string>>>(*other.m_mapping);
        m_reverseMapping = std::make_unique<std::map<size_t, std::map<std::string, std::vector<u8>, std::less<>>>>(*other.m_reverseMapping);
        m_tableContent = other.m_tableContent;
        m_longestSequence = other.m_longestSequence;
        m_shortestSequence = other.m_shortestSequence;
        m_ambiguousEncoding = other.m_ambiguousEncoding;
        m_valid = other.m_valid;
        m_name = other.m_name;

        return *this;
    }

    EncodingFile &EncodingFile::operator=(EncodingFile &&other) noexcept {
        m_mapping = std::move(other.m_mapping);
        m_reverseMapping = std::move(other.m_reverseMapping);
        m_tableContent = std::move(other.m_tableContent);
        m_longestSequence = other.m_longestSequence;
        m_shortestSequence = other.m_shortestSequence;
        m_ambiguousEncoding = other.m_ambiguousEncoding;
        m_valid = other.m_valid;
        m_name = std::move(other.m_name);

        return *this;
    }



    std::optional<std::pair<std::string_view, size_t>> EncodingFile::lookup(std::span<const u8> buffer) const {
        for (const auto &[size, mapping] : std::ranges::reverse_view(*m_mapping)) {
            if (size > buffer.size()) continue;

            std::vector key(buffer.begin(), buffer.begin() + size);
            if (const auto entry = mapping.find(key); entry != mapping.end())
                return std::pair<std::string_view, size_t>{ entry->second, size };
        }

        return std::nullopt;
    }

    std::pair<std::string_view, size_t> EncodingFile::getEncodingFor(std::span<const u8> buffer) const {
        return this->lookup(buffer).value_or(std::pair<std::string_view, size_t>{ ".", 1 });
    }

    u64 EncodingFile::getEncodingLengthFor(std::span<u8> buffer) const {
        for (const auto& [size, mapping] : std::ranges::reverse_view(*m_mapping)) {
            if (size > buffer.size()) continue;

            std::vector key(buffer.begin(), buffer.begin() + size);
            if (mapping.contains(key))
                return size;
        }

        return 1;
    }

    std::string EncodingFile::decodeAll(std::span<const u8> buffer) const {
        std::string result;

        while (!buffer.empty()) {
            const auto [character, size] = getEncodingFor(buffer);
            result += character;
            buffer = buffer.subspan(size);
        }

        return result;
    }

    std::optional<std::pair<std::vector<u8>, size_t>> EncodingFile::getBytesFor(std::string_view sequence) const {
        for (const auto &[size, mapping] : std::ranges::reverse_view(*m_reverseMapping)) {
            if (size > sequence.size()) continue;

            auto key = sequence.substr(0, size);
            auto iter = mapping.find(key);
            if (iter != mapping.end())
                return std::pair { iter->second, size };
        }

        return std::nullopt;
    }

    std::optional<std::vector<u8>> EncodingFile::encodeAll(std::string_view sequence) const {
        if (!canEncode())
            return std::nullopt;

        std::vector<u8> result;

        while (!sequence.empty()) {
            auto match = getBytesFor(sequence);
            if (!match.has_value())
                return std::nullopt;

            const auto &[bytes, size] = match.value();
            result.insert(result.end(), bytes.begin(), bytes.end());
            sequence = sequence.substr(size);
        }

        return result;
    }


    void EncodingFile::parse(const std::string &content) {
        m_tableContent = content;

        // Every decoded value seen so far. This detects a duplicate target:
        // the same decoded value produced by more than one byte sequence.
        // A duplicate target makes the encoding ambiguous.
        std::vector<std::string_view> encodedValues;

        for (const auto &line : wolv::util::splitString(m_tableContent, "\n")) {

            std::string from, to;
            {
                auto delimiterPos = line.find('=');

                if (delimiterPos >= line.length())
                    continue;

                from = line.substr(0, delimiterPos);
                to   = line.substr(delimiterPos + 1);

                if (from.empty()) continue;
            }

            auto fromBytes = hex::parseByteString(from);
            if (fromBytes.empty()) continue;

            if (to.length() > 1)
                to = wolv::util::trim(to);
            if (to.empty())
                to = " ";

            if (!m_mapping->contains(fromBytes.size()))
                m_mapping->insert({ fromBytes.size(), {} });

            u64 keySize = fromBytes.size();
            u64 valueSize = to.size();

            bool isStandardAsciiEntry = keySize == 1 && fromBytes[0] <= 0x7F;
            if (isStandardAsciiEntry) {
                // Some tables, like EBCDIC, redefine the 0x00-0x7F range. This byte is
                // checked like any other in that case.
                if (to != StandardAsciiRange[fromBytes[0]])
                    isStandardAsciiEntry = false;
            }

            if (!m_reverseMapping->contains(valueSize))
                m_reverseMapping->insert({ valueSize, {} });

            auto &reverseBucket = (*m_reverseMapping)[valueSize];
            auto existingEntry = reverseBucket.find(to);
            if (existingEntry == reverseBucket.end()) {
                auto iter = reverseBucket.emplace(to, fromBytes).first;

                if (!isStandardAsciiEntry)
                    encodedValues.emplace_back(iter->first);
            } else if (existingEntry->second != fromBytes && !isStandardAsciiEntry) {
                // A different byte sequence produced the same decoded value. That is
                // a real conflict. An exact duplicate line, with the same "from" and
                // "to" values, is not a conflict.
                m_ambiguousEncoding = true;
            }

            (*m_mapping)[keySize].insert({ std::move(fromBytes), to });

            m_longestSequence = std::max(m_longestSequence, keySize);
            m_shortestSequence = std::min(m_shortestSequence, keySize);
        }

        // A byte in 0x00-0x7F the table does not map defaults to standard ASCII, skipping the
        // ambiguity check above (see isStandardAsciiEntry); it never replaces a real table entry.
        auto &byteMapping = (*m_mapping)[1];
        for (int byte = 0x00; byte <= 0x7F; byte++) {
            std::vector<u8> key { static_cast<u8>(byte) };
            if (byteMapping.contains(key))
                continue;

            std::string_view text = StandardAsciiRange[byte];
            byteMapping.emplace(key, text);

            auto &reverseBucket = (*m_reverseMapping)[text.size()];
            if (!reverseBucket.contains(text))
                reverseBucket.emplace(text, key);
        }
        m_longestSequence = std::max(m_longestSequence, u64(1));
        m_shortestSequence = std::min(m_shortestSequence, u64(1));

        // A prefix-free code decodes to one unique result: no encoded value is a prefix of
        // another. Sufficient but not necessary for uniqueness - the full test is the
        // Sardinas-Patterson algorithm. This check is O(n log n); some tables, like utf8.tbl,
        // have over 100,000 entries.
        if (!m_ambiguousEncoding) {
            std::ranges::sort(encodedValues);
            for (size_t i = 1; i < encodedValues.size(); i++) {
                if (encodedValues[i].starts_with(encodedValues[i - 1])) {
                    m_ambiguousEncoding = true;
                    break;
                }
            }
        }
    }


    const EncodingFile* getEncodingByName(const std::string &name) {
        static std::mutex mutex;
        static std::map<std::string, EncodingFile> encodings;

        std::scoped_lock lock(mutex);

        if (const auto entry = encodings.find(name); entry != encodings.end())
            return entry->second.valid() ? &entry->second : nullptr;

        // An encoding name is conventionally case-insensitive. So "UTF-8" and
        // "utf-8" find the same table.
        const auto lowerCaseName = toLower(name);

        auto path = findEncodingFile(name);

        // Rejects a direct file match when `name` is itself the internal file stem for an
        // encoding EncodingNameAliases already gives a real name to. A stem with no alias
        // entry has no such replacement, and keeps working under its current name.
        if (path.has_value()) {
            const bool nameIsBareStem = std::ranges::any_of(EncodingNameAliases, [&](const auto &entry) {
                return entry.second == lowerCaseName;
            }) && std::ranges::none_of(EncodingNameAliases, [&](const auto &entry) {
                return entry.first == lowerCaseName;
            });

            if (nameIsBareStem)
                path.reset();
        }

        if (!path.has_value()) {
            for (const auto &[alias, fileStem] : EncodingNameAliases) {
                if (alias != lowerCaseName)
                    continue;

                path = findEncodingFile(fileStem);
                break;
            }
        }

        EncodingFile encoding;
        if (path.has_value())
            encoding = EncodingFile(EncodingFile::Type::Thingy, *path);

        // A failed lookup is cached too. So a name that does not exist does
        // not reach the file system again on every following call.
        const auto &result = encodings.emplace(name, std::move(encoding)).first->second;
        return result.valid() ? &result : nullptr;
    }

    const Codepage& Codepage::ascii() {
        static const Codepage asciiCodepage = [] {
            Codepage result;
            for (size_t byte = 0; byte < StandardAsciiRange.size(); byte += 1) {
                // This leaves the control codes empty. StandardAsciiRange spells a
                // control code out as a name, like "NUL" or "SOH". A name is not a
                // character that can be drawn.
                if (const auto text = StandardAsciiRange[byte]; isSingleCharacter(text))
                    result.m_characters[byte] = text;
            }

            return result;
        }();

        return asciiCodepage;
    }

    std::optional<Codepage> Codepage::fromEncoding(const EncodingFile &encoding) {
        if (!encoding.valid() || encoding.getLongestSequence() != 1)
            return std::nullopt;

        Codepage result;
        result.m_name = encoding.getName();

        for (size_t byte = 0; byte <= 0xFF; byte += 1) {
            const auto key = u8(byte);
            const auto entry = encoding.lookup(std::span(&key, 1));
            if (!entry.has_value())
                continue;

            if (const auto text = entry->first; isSingleCharacter(text))
                result.m_characters[byte] = text;
        }

        return result;
    }

    std::string decodeForDisplay(const EncodingFile &encoding, std::span<const u8> bytes) {
        std::string result;

        while (!bytes.empty()) {
            const auto [decoded, advance] = encoding.getEncodingFor(bytes);

            // A backslash is escaped even though it is a fine character on its
            // own. This keeps it from being mistaken for one of the escape
            // sequences below.
            if (decoded != "\\" && isSingleCharacter(decoded)) {
                result += decoded;
            } else if (decoded != "\\" && hasNonAsciiByte(decoded)) {
                // Real decoded text, but not a single cell's worth of it - escapes its own
                // codepoints, not the input bytes that produced it.
                appendUnicodeCodepoints(result, decoded);
            } else {
                for (const u8 byte : bytes.subspan(0, advance))
                    appendEscapedByte(result, byte);
            }

            bytes = bytes.subspan(advance);
        }

        return result;
    }

    std::optional<std::string> decodeUtf8(std::span<const u8> bytes) {
        std::string result;
        while (!bytes.empty()) {
            const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            const auto [codepoint, length] = utf8Decode(text);
            std::ignore = codepoint;

            if (length == 0)
                return std::nullopt;

            result.append(text.substr(0, length));
            bytes = bytes.subspan(length);
        }

        return result;
    }

    std::optional<std::string> decodeUtf16(std::span<const u8> bytes, std::endian endian) {
        // Byte-swaps a little endian pair read from `bytes` into the requested endianness.
        const auto readUnit = [endian](std::span<const u8> unitBytes) -> u16 {
            u16 unit = u16(unitBytes[0]) | (u16(unitBytes[1]) << 8);
            if (endian == std::endian::big)
                unit = u16((unit << 8) | (unit >> 8));
            return unit;
        };

        std::string result;
        while (bytes.size() >= 2) {
            const u16 unit = readUnit(bytes.subspan(0, 2));

            const bool isHighSurrogate = unit >= 0xD800 && unit <= 0xDBFF;
            const bool isLowSurrogate  = unit >= 0xDC00 && unit <= 0xDFFF;

            u32 codepoint  = unit;
            size_t advance = 2;
            bool valid     = !isHighSurrogate && !isLowSurrogate;

            if (isHighSurrogate && bytes.size() >= 4) {
                if (const u16 low = readUnit(bytes.subspan(2, 2)); low >= 0xDC00 && low <= 0xDFFF) {
                    codepoint = 0x10000 + (u32(unit - 0xD800) << 10) + (low - 0xDC00);
                    advance   = 4;
                    valid     = true;
                }
            }

            if (valid) {
                if (auto utf8 = wolv::util::utf32ToUtf8(std::u32string(1, char32_t(codepoint))); utf8.has_value()) {
                    result += *utf8;
                    bytes = bytes.subspan(advance);
                    continue;
                }
            }

            return std::nullopt;
        }

        if (!bytes.empty())
            return std::nullopt;

        return result;
    }

    std::optional<std::string> decodeUtf32(std::span<const u8> bytes, std::endian endian) {
        std::string result;
        while (bytes.size() >= 4) {
            u32 codepoint = u32(bytes[0]) | (u32(bytes[1]) << 8) | (u32(bytes[2]) << 16) | (u32(bytes[3]) << 24);
            if (endian == std::endian::big)
                codepoint = ((codepoint & 0x000000FF) << 24) | ((codepoint & 0x0000FF00) << 8)
                          | ((codepoint & 0x00FF0000) >> 8)  | ((codepoint & 0xFF000000) >> 24);

            const bool valid = codepoint <= 0x10FFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF);
            if (!valid)
                return std::nullopt;

            auto utf8 = wolv::util::utf32ToUtf8(std::u32string(1, char32_t(codepoint)));
            if (!utf8.has_value())
                return std::nullopt;

            result += *utf8;
            bytes = bytes.subspan(4);
        }

        if (!bytes.empty())
            return std::nullopt;

        return result;
    }

    std::string decodeUtf8Lossy(std::span<const u8> bytes) {
        std::string result;
        while (!bytes.empty()) {
            const std::string_view text(reinterpret_cast<const char *>(bytes.data()), bytes.size());
            const auto [codepoint, length] = utf8Decode(text);
            std::ignore = codepoint;

            if (length == 0) {
                result += "\xEF\xBF\xBD"; // U+FFFD
                bytes = bytes.subspan(1);
                continue;
            }

            result.append(text.substr(0, length));
            bytes = bytes.subspan(length);
        }

        return result;
    }

    std::string decodeUtf16Lossy(std::span<const u8> bytes, std::endian endian) {
        const auto readUnit = [endian](std::span<const u8> unitBytes) -> u16 {
            u16 unit = u16(unitBytes[0]) | (u16(unitBytes[1]) << 8);
            if (endian == std::endian::big)
                unit = u16((unit << 8) | (unit >> 8));
            return unit;
        };

        std::string result;
        while (bytes.size() >= 2) {
            const u16 unit = readUnit(bytes.subspan(0, 2));

            const bool isHighSurrogate = unit >= 0xD800 && unit <= 0xDBFF;
            const bool isLowSurrogate  = unit >= 0xDC00 && unit <= 0xDFFF;

            u32 codepoint  = unit;
            size_t advance = 2;
            bool valid     = !isHighSurrogate && !isLowSurrogate;

            if (isHighSurrogate && bytes.size() >= 4) {
                if (const u16 low = readUnit(bytes.subspan(2, 2)); low >= 0xDC00 && low <= 0xDFFF) {
                    codepoint = 0x10000 + (u32(unit - 0xD800) << 10) + (low - 0xDC00);
                    advance   = 4;
                    valid     = true;
                }
            }

            if (valid) {
                if (auto utf8 = wolv::util::utf32ToUtf8(std::u32string(1, char32_t(codepoint))); utf8.has_value()) {
                    result += *utf8;
                    bytes = bytes.subspan(advance);
                    continue;
                }
            }

            result += "\xEF\xBF\xBD"; // U+FFFD
            bytes = bytes.subspan(advance);
        }

        // A single trailing byte with no partner decodes as replacement, not as raw text.
        if (!bytes.empty())
            result += "\xEF\xBF\xBD";

        return result;
    }

    std::string decodeUtf32Lossy(std::span<const u8> bytes, std::endian endian) {
        std::string result;
        while (bytes.size() >= 4) {
            u32 codepoint = u32(bytes[0]) | (u32(bytes[1]) << 8) | (u32(bytes[2]) << 16) | (u32(bytes[3]) << 24);
            if (endian == std::endian::big)
                codepoint = ((codepoint & 0x000000FF) << 24) | ((codepoint & 0x0000FF00) << 8)
                          | ((codepoint & 0x00FF0000) >> 8)  | ((codepoint & 0xFF000000) >> 24);

            const bool valid = codepoint <= 0x10FFFF && !(codepoint >= 0xD800 && codepoint <= 0xDFFF);

            if (valid) {
                if (auto utf8 = wolv::util::utf32ToUtf8(std::u32string(1, char32_t(codepoint))); utf8.has_value()) {
                    result += *utf8;
                    bytes = bytes.subspan(4);
                    continue;
                }
            }

            result += "\xEF\xBF\xBD"; // U+FFFD
            bytes = bytes.subspan(4);
        }

        // Up to three trailing bytes with no complete unit decode as one replacement character.
        if (!bytes.empty())
            result += "\xEF\xBF\xBD";

        return result;
    }

    std::vector<u8> encodeUtf8(std::string_view text) {
        return { text.begin(), text.end() };
    }

    std::vector<u8> encodeUtf16(std::string_view text, std::endian endian) {
        std::vector<u8> result;

        const auto pushUnit = [&](u16 unit) {
            if (endian == std::endian::big)
                unit = u16((unit << 8) | (unit >> 8));
            result.push_back(u8(unit & 0xFF));
            result.push_back(u8((unit >> 8) & 0xFF));
        };

        while (!text.empty()) {
            const auto [codepoint, length] = utf8Decode(text);
            if (length == 0) {
                // Malformed input UTF-8. Should not happen; the pattern language's own source
                // and string literals are UTF-8. Skip the byte rather than lose the rest.
                text = text.substr(1);
                continue;
            }

            if (codepoint <= 0xFFFF) {
                pushUnit(u16(codepoint));
            } else {
                const u32 value = codepoint - 0x10000;
                pushUnit(u16(0xD800 + (value >> 10)));
                pushUnit(u16(0xDC00 + (value & 0x3FF)));
            }

            text = text.substr(length);
        }

        return result;
    }

    std::vector<u8> encodeUtf32(std::string_view text, std::endian endian) {
        std::vector<u8> result;

        while (!text.empty()) {
            const auto [codepoint, length] = utf8Decode(text);
            if (length == 0) {
                text = text.substr(1);
                continue;
            }

            u32 value = codepoint;
            if (endian == std::endian::big)
                value = ((value & 0x000000FF) << 24) | ((value & 0x0000FF00) << 8)
                      | ((value & 0x00FF0000) >> 8)  | ((value & 0xFF000000) >> 24);

            result.push_back(u8(value & 0xFF));
            result.push_back(u8((value >> 8) & 0xFF));
            result.push_back(u8((value >> 16) & 0xFF));
            result.push_back(u8((value >> 24) & 0xFF));

            text = text.substr(length);
        }

        return result;
    }

}
