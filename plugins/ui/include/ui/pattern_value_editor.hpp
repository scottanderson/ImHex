#pragma once

#include <hex.hpp>
#include <pl/pattern_visitor.hpp>
#include <functional>
#include <string>

namespace hex::ui {

    class PatternValueEditor : public pl::PatternVisitor {
    public:
        PatternValueEditor() = default;
        explicit PatternValueEditor(const std::function<void()>& onEditCallback) : m_onEditCallback(onEditCallback) {}

        void visit(pl::ptrn::PatternArrayDynamic& pattern) override;
        void visit(pl::ptrn::PatternArrayStatic& pattern) override;
        void visit(pl::ptrn::PatternBitfield& pattern) override;
        void visit(pl::ptrn::PatternBitfieldField& pattern) override;
        void visit(pl::ptrn::PatternBitfieldArray& pattern) override;
        void visit(pl::ptrn::PatternBoolean& pattern) override;
        void visit(pl::ptrn::PatternCharacter& pattern) override;
        void visit(pl::ptrn::PatternEnum& pattern) override;
        void visit(pl::ptrn::PatternFloat& pattern) override;
        void visit(pl::ptrn::PatternPadding& pattern) override;
        void visit(pl::ptrn::PatternPointer& pattern) override;
        void visit(pl::ptrn::PatternSigned& pattern) override;
        void visit(pl::ptrn::PatternString& pattern) override;
        void visit(pl::ptrn::PatternStruct& pattern) override;
        void visit(pl::ptrn::PatternUnion& pattern) override;
        void visit(pl::ptrn::PatternUnsigned& pattern) override;
        void visit(pl::ptrn::PatternWideCharacter& pattern) override;
        void visit(pl::ptrn::PatternWideString& pattern) override;
        void visit(pl::ptrn::PatternError& pattern) override;
        void visit(pl::ptrn::Pattern& pattern) override;

        // Drops the field currently being edited, if any. Call this when the
        // pattern tree is about to be rebuilt: the freed node's address can be
        // reused by an unrelated field, which would otherwise pass the pointer
        // check below and show that field's stale text.
        void resetEditing();

    private:
        // Leaves edit mode without committing, on Escape or a click elsewhere.
        // `submitted` is true only on Enter; that case commits and is not a cancel.
        void cancelIfDeactivated(bool submitted);

        std::function<void()> m_onEditCallback = [](){};

        // True while the string field currently being typed into has a character
        // its encoding cannot represent. Only one field is ever in edit mode at a
        // time. One flag is enough; it does not need to be keyed per pattern.
        bool m_hasUnencodableChar = false;

        // Holds the live text of the string field currently being edited, across
        // frames. Enter deactivates the underlying input widget whether or not the
        // edit is accepted, so re-deriving this from the pattern's own value on
        // every call would discard a rejected edit instead of leaving it in place
        // to fix. Reset only when editing starts on a different pattern.
        std::string m_editingValue;
        const pl::ptrn::Pattern *m_editingValuePattern = nullptr;
    };

}
