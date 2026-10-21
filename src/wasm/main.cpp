#include <emscripten/bind.h>

#include <cstdio>
#include <filesystem>
#include <memory>
#include <system_error>
#include <string>
#include <string_view>
#include <vector>

#include "core/config/config.h"
#include "core/engine.h"
#include "core/engine/driver.h"
#include "core/engine/trace.h"
#include "core/userdict/userdict.h"
#include "frontend/dictionary_version.h"

namespace {

using emscripten::val;

unsigned nextUserFile = 0;

val Selection(const hdTrans::internal::TraceSelection& selection) {
    auto out = val::object();
    out.set("index", selection.index);
    out.set("text", selection.text);
    out.set("provenance", selection.provenance);
    out.set("layer", selection.layer);
    out.set("id", selection.id);
    out.set("source", selection.source);
    return out;
}

val Result(const hdTrans::internal::TranslationTrace& trace) {
    auto choices = val::array();
    unsigned i = 0;
    for (const auto& choice : trace.choices) {
        auto row = val::object();
        row.set("line", choice.line);
        row.set("chunk", choice.chunk);
        row.set("sentence", choice.sentence);
        row.set("token", choice.token);
        row.set("start", choice.start);
        row.set("surface", choice.surface);
        row.set("choice", Selection(choice.choice));
        auto ending = val::null();
        if (choice.ending) {
            ending = Selection(choice.ending->choice);
            ending.set("surface", choice.ending->surface);
        }
        row.set("ending", ending);
        choices.set(i++, row);
    }
    auto logs = val::array();
    i = 0;
    for (const auto& log : trace.logs) logs.set(i++, log);
    auto out = val::object();
    out.set("text", trace.text);
    out.set("choices", choices);
    out.set("logs", logs);
    return out;
}

class Translator {
public:
    Translator() : userFile_("/hdtrans-userdict-" + std::to_string(++nextUserFile) + ".tsv") {}
    ~Translator() { Close(); }

    bool Open(const val& directories) {
        Close();
        directories_ = emscripten::vecFromJSArray<std::string>(directories);
        engine_ = std::make_unique<hdTrans::Engine>(false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Off);
        if (!engine_->Open(directories_, false, userFile_)) {
            error_ = "hdtrans: the dictionaries do not load";
            engine_.reset();
            return false;
        }
        session_ = std::make_unique<hdTrans::Session>(*engine_);
        session_->SetSpeech(speech_);
        dictionaryVersion_ = hdTrans::frontend::DictionaryVersion(directories_);
        error_.clear();
        return true;
    }

    std::string Translate(const std::string& text) {
        if (!Ready()) return {};
        return session_->Translate(text);
    }

    val TranslateJson(const std::string& text) {
        if (!Ready()) return val::null();
        hdTrans::internal::TraceCollector collector;
        collector.result.text = hdTrans::internal::TranslateObserved(*session_, text,
            hdTrans::internal::TraceObserver(collector));
        return Result(collector.result);
    }

    bool Reset() {
        if (!Ready()) return false;
        session_->Reset();
        return true;
    }

    // soft or strict: the speech level of later translations.
    bool SetSpeech(const std::string& level) {
        if (!hdTrans::internal::SpeechOf(level, speech_)) {
            error_ = "hdtrans: speech takes soft or strict";
            return false;
        }
        if (session_) session_->SetSpeech(speech_);
        return true;
    }

    bool SetUserDictRaw(const std::string& text) {
        if (!Ready()) return false;
        if (text.empty()) {
            std::remove(userFile_.c_str());
        } else {
            std::FILE* file = std::fopen(userFile_.c_str(), "wb");
            if (!file) {
                error_ = "hdtrans: cannot write the user dictionary";
                return false;
            }
            const bool written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
            const bool closed = std::fclose(file) == 0;
            if (!written || !closed) {
                error_ = "hdtrans: cannot write the user dictionary";
                return false;
            }
        }
        if (!engine_->Reload()) {
            if (!engine_->UserFileError().empty()) return true;
            error_ = "hdtrans: the dictionaries do not load";
            return false;
        }
        session_->Reset();
        userEntries_.clear();
        if (engine_->UserFileError().empty()) {
            hdTrans::dict::CodeTable codes;
            hdTrans::userdict::Families families;
            const auto snapshot = engine_->Dictionaries();
            hdTrans::userdict::Context context{&codes, &families, &snapshot->Base()};
            hdTrans::userdict::Parse(text, 0, context, &userEntries_);
        }
        return true;
    }

    val GetUserDict() const {
        auto out = val::array();
        unsigned i = 0;
        for (const auto& entry : userEntries_) {
            auto row = val::object();
            row.set("ja", entry.ja);
            row.set("ko", entry.ko);
            if (!entry.pos.empty()) row.set("pos", entry.pos);
            if (!entry.options.empty()) row.set("options", entry.options);
            out.set(i++, row);
        }
        return out;
    }

    bool SetEhnd(const val& files) {
        if (!Ready()) return false;
        const auto names = emscripten::vecFromJSArray<std::string>(val::global("Object").call<val>("keys", files));
        if (names.empty()) {
            if (!engine_->OpenEhnd("")) return false;
            RemoveEhnd();
            session_->Reset();
            return true;
        }
        for (const auto& name : names) {
            if (name.empty() || name == "." || name == ".." || name.find_first_of("/\\\0", 0, 3) != std::string::npos) {
                error_ = "hdtrans: invalid Ehnd filename";
                return false;
            }
        }
        const std::string folder = userFile_ + "-ehnd-" + std::to_string(++ehndGeneration_);
        std::error_code error;
        std::filesystem::create_directory(folder, error);
        if (error) {
            error_ = "hdtrans: cannot create the Ehnd folder";
            return false;
        }
        for (const auto& name : names) {
            const std::string text = files[name].as<std::string>();
            std::FILE* file = std::fopen((folder + "/" + name).c_str(), "wb");
            bool written = false;
            if (file) {
                written = std::fwrite(text.data(), 1, text.size(), file) == text.size();
                written = std::fclose(file) == 0 && written;
            }
            if (!written) {
                std::filesystem::remove_all(folder, error);
                error_ = "hdtrans: cannot write the Ehnd file";
                return false;
            }
        }
        if (!engine_->OpenEhnd(folder)) {
            std::filesystem::remove_all(folder, error);
            error_ = "hdtrans: the Ehnd filters do not load";
            return false;
        }
        RemoveEhnd();
        ehndDir_ = folder;
        session_->Reset();
        return true;
    }

    std::string UserDictError() const {
        if (!engine_) return {};
        std::string errors = engine_->UserFileError();
        std::size_t start = 0;
        while ((start = errors.find(userFile_, start)) != std::string::npos) {
            errors.replace(start, userFile_.size(), "userdict.tsv");
            start += std::string_view("userdict.tsv").size();
        }
        return errors;
    }

    val FilterErrors() const {
        auto out = val::array();
        if (engine_) {
            unsigned i = 0;
            for (const auto& error : engine_->FilterErrors()) out.set(i++, error);
        }
        return out;
    }

    std::string Error() const { return error_; }
    std::string DictionaryVersion() const { return dictionaryVersion_; }

    void Close() {
        session_.reset();
        engine_.reset();
        userEntries_.clear();
        std::remove(userFile_.c_str());
        RemoveEhnd();
    }

private:
    void RemoveEhnd() {
        if (ehndDir_.empty()) return;
        std::error_code error;
        std::filesystem::remove_all(ehndDir_, error);
        ehndDir_.clear();
    }

    bool Ready() {
        error_ = session_ ? "" : "hdtrans: the translator is closed";
        return session_ != nullptr;
    }

    const std::string userFile_;
    std::string ehndDir_;
    unsigned ehndGeneration_ = 0;
    std::vector<std::string> directories_;
    std::vector<hdTrans::userdict::SourceEntry> userEntries_;
    std::unique_ptr<hdTrans::Engine> engine_;
    std::unique_ptr<hdTrans::Session> session_;
    hdTrans::Speech speech_ = hdTrans::Speech::Soft;
    std::string error_;
    std::string dictionaryVersion_;
};

std::string Version() { return HDTRANS_VERSION; }

}  // namespace

EMSCRIPTEN_BINDINGS(hdtrans) {
    emscripten::class_<Translator>("Translator")
        .constructor<>()
        .function("open", &Translator::Open)
        .function("translate", &Translator::Translate)
        .function("translateJson", &Translator::TranslateJson)
        .function("reset", &Translator::Reset)
        .function("setSpeech", &Translator::SetSpeech)
        .function("setUserDictRaw", &Translator::SetUserDictRaw)
        .function("getUserDict", &Translator::GetUserDict)
        .function("setEhnd", &Translator::SetEhnd)
        .function("close", &Translator::Close)
        .property("error", &Translator::Error)
        .property("dictionaryVersion", &Translator::DictionaryVersion)
        .property("userDictError", &Translator::UserDictError)
        .property("filterErrors", &Translator::FilterErrors);
    emscripten::function("version", &Version);
}
