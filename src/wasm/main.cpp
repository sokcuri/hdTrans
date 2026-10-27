#include <emscripten/bind.h>

#include <memory>
#include <string>
#include <vector>

#include "core/config/config.h"
#include "core/engine.h"
#include "core/engine/driver.h"
#include "core/engine/trace.h"
#include "frontend/dictionary_version.h"

namespace {

using emscripten::val;

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
    Translator() = default;
    ~Translator() { Close(); }

    bool Open(const val& directories, const val& strictDict) {
        Close();
        directories_ = emscripten::vecFromJSArray<std::string>(directories);
        hdTrans::EngineOptions options;
        if (!strictDict.isUndefined()) options.strictDict = strictDict.as<bool>();
        engine_ = std::make_unique<hdTrans::Engine>(
            false, hdTrans::CacheWrite::Off, hdTrans::UserReload::Off, options);
        if (!engine_->Open(directories_)) {
            LoadError("hdtrans: the dictionaries do not load");
            engine_.reset();
            return false;
        }
        session_ = std::make_unique<hdTrans::Session>(*engine_);
        session_->SetSpeech(speech_);
        session_->SetWrapped(wrapped_);
        dictionaryVersion_ = hdTrans::frontend::DictionaryVersion(directories_);
        error_.clear();
        return true;
    }

    // context: "\n"-separated lines read before text (Session::Translate); "" for none.
    std::string Translate(const std::string& text, const std::string& context) {
        if (!Ready()) return {};
        return session_->Translate(text, context);
    }

    val TranslateJson(const std::string& text, const std::string& context) {
        if (!Ready()) return val::null();
        hdTrans::internal::TraceCollector collector;
        collector.result.text = hdTrans::internal::TranslateObserved(*session_, text,
            hdTrans::internal::TraceObserver(collector), context);
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

    void SetWrapped(bool wrapped) {
        wrapped_ = wrapped;
        if (session_) session_->SetWrapped(wrapped_);
    }

    bool SetUserDict(const val& rows, const val& strict) {
        if (!Ready()) return false;
        std::vector<hdTrans::UserDictEntry> entries;
        const unsigned length = rows["length"].as<unsigned>();
        entries.reserve(length);
        for (unsigned i = 0; i < length; ++i) {
            const auto row = rows[i];
            entries.push_back({row["ja"].as<std::string>(), row["ko"].as<std::string>(),
                               row["pos"].as<std::string>(), row["options"].as<std::string>()});
        }
        if (!engine_->SetUserDict(entries, Options(strict))) {
            return LoadError("hdtrans: the user dictionary does not load");
        }
        session_->Reset();
        return true;
    }

    bool SetUserDictRaw(const std::string& text, const val& strict) {
        if (!Ready()) return false;
        if (!engine_->SetUserDictRaw(text, Options(strict))) {
            return LoadError("hdtrans: the user dictionary does not load");
        }
        session_->Reset();
        return true;
    }

    val GetUserDict() const {
        auto out = val::array();
        if (!engine_) return out;
        unsigned i = 0;
        for (const auto& entry : engine_->GetUserDict()) {
            auto row = val::object();
            row.set("ja", entry.ja);
            row.set("ko", entry.ko);
            if (!entry.pos.empty()) row.set("pos", entry.pos);
            if (!entry.options.empty()) row.set("options", entry.options);
            out.set(i++, row);
        }
        return out;
    }

    bool SetEhnd(const val& files, const val& strict) {
        if (!Ready()) return false;
        const auto names = emscripten::vecFromJSArray<std::string>(val::global("Object").call<val>("keys", files));
        std::vector<hdTrans::EhndFile> sources;
        sources.reserve(names.size());
        for (const auto& name : names) sources.push_back({name, files[name].as<std::string>()});
        if (!engine_->SetEhnd(sources, Options(strict))) {
            return LoadError("hdtrans: the Ehnd filters do not load");
        }
        session_->Reset();
        return true;
    }

    std::string UserDictError() const {
        return engine_ ? engine_->UserFileError() : std::string{};
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
    }

private:
    static hdTrans::DictionaryOptions Options(const val& strict) {
        hdTrans::DictionaryOptions options;
        if (!strict.isUndefined()) options.strict = strict.as<bool>();
        return options;
    }

    bool LoadError(const char* message) {
        error_ = message;
        const auto user = engine_->UserFileError();
        if (!user.empty()) error_ += "\n" + user;
        for (const auto& error : engine_->FilterErrors()) error_ += "\n" + error;
        return false;
    }

    bool Ready() {
        error_ = session_ ? "" : "hdtrans: the translator is closed";
        return session_ != nullptr;
    }

    std::vector<std::string> directories_;
    std::unique_ptr<hdTrans::Engine> engine_;
    std::unique_ptr<hdTrans::Session> session_;
    hdTrans::Speech speech_ = hdTrans::Speech::Strict;
    bool wrapped_ = false;
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
        .function("setWrapped", &Translator::SetWrapped)
        .function("setUserDict", &Translator::SetUserDict)
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
