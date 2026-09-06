#pragma once

#include <string>
#include <string_view>

namespace hex::ui {

    // NUL is the one control byte an edit box cannot show as-is: ImGui's
    // InputText is null-terminated, so a raw NUL ends the buffer right there.
    // Every other control byte (0x01-0x1F, 0x7F) is a fine, if invisible,
    // byte in the buffer and needs no substitute - the font draws a glyph
    // for it directly (see hex::isControlCode() in encoding_file.hpp).
    //
    // Substituted only for display: shown as U+2400 (3-byte UTF-8, 0xE2 0x90
    // 0x80) while editing, converted back before every encode check and
    // commit, so the pattern's value is always the real byte, never the
    // picture. The one exception is Ctrl+C: ImGui's InputText copies the
    // edit buffer straight to the OS clipboard, picture and all, since
    // converting first would truncate the copy at the NUL.
    //
    // The substitution is not reversible for text that already holds a real
    // U+2400. pictureToNul() turns that character into a NUL byte, because
    // nothing in a NUL-terminated buffer can tell the two apart. Editing such
    // a string changes it. This is the cost of editing through InputText at
    // all; only a length-carrying edit buffer would avoid it.
    constexpr inline char NulPicture[] = { char(0xE2), char(0x90), char(0x80) };

    // Replaces each NUL byte with the NUL picture character, so an edit box can
    // hold the whole value.
    inline std::string nulToPicture(std::string_view text) {
        std::string result;
        for (const char byte : text) {
            if (byte == '\0')
                result.append(NulPicture, sizeof(NulPicture));
            else
                result += byte;
        }
        return result;
    }

    // Inverse of nulToPicture(), for text on its way back out of an edit box.
    inline std::string pictureToNul(std::string_view text) {
        std::string result;
        for (size_t i = 0; i < text.size(); i += 1) {
            if (i + sizeof(NulPicture) <= text.size() && text.substr(i, sizeof(NulPicture)) == std::string_view(NulPicture, sizeof(NulPicture))) {
                result += '\0';
                i += sizeof(NulPicture) - 1;
            } else {
                result += text[i];
            }
        }
        return result;
    }

}
