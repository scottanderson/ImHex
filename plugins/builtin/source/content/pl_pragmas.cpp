#include <hex/api/imhex_api/provider.hpp>
#include <hex/api/imhex_api/hex_editor.hpp>
#include <hex/api/content_registry/pattern_language.hpp>
#include <hex/api/task_manager.hpp>
#include <hex/providers/provider.hpp>
#include <hex/helpers/encoding_file.hpp>
#include <hex/helpers/magic.hpp>
#include <hex/helpers/utils.hpp>

#include <pl/core/evaluator.hpp>

namespace hex::plugin::builtin {

    void registerPatternLanguagePragmas() {

        ContentRegistry::PatternLanguage::addPragma("base_address", [](pl::PatternLanguage &runtime, const std::string &value) {
            auto baseAddress = wolv::util::from_chars<u64>(value);
            u64 dataSize = runtime.getInternals().evaluator->getDataSize();
            if (!baseAddress.has_value() || (dataSize > 0 && dataSize - 1 > std::numeric_limits<u64>::max() - baseAddress.value()))
                return false;

            if (ImHexApi::Provider::isValid())
                ImHexApi::Provider::get()->setBaseAddress(*baseAddress);
            runtime.setDataBaseAddress(*baseAddress);

            return true;
        });

        ContentRegistry::PatternLanguage::addPragma("encoding", [](pl::PatternLanguage&, const std::string &value) {
            if (getEncodingByName(value) == nullptr)
                return false;

            // A pragma runs on the pattern evaluation thread; only the main thread may change
            // the hex editor's state.
            TaskManager::doLater([value] {
                ImHexApi::HexEditor::setEncoding(value);
            });

            return true;
        });

        ContentRegistry::PatternLanguage::addPragma("MIME", [](pl::PatternLanguage&, const std::string &value) {
            return magic::isValidMIMEType(value);
        });

        ContentRegistry::PatternLanguage::addPragma("magic", [](pl::PatternLanguage&, const std::string &) {
            return true;
        });
    }

}
