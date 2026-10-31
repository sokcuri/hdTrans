#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "core/dict/load_info.h"

namespace hdTrans::frontend {
namespace internal {

inline std::string LoadInfoPath(std::string_view path, std::string_view programDir) {
    if (programDir.empty() || path.empty()) return std::string(path);
    const auto toPath = [](std::string_view text) {
        return std::filesystem::path(std::u8string(reinterpret_cast<const char8_t*>(text.data()), text.size()));
    };
    const auto text = toPath(path).lexically_proximate(toPath(programDir)).u8string();
    return {reinterpret_cast<const char*>(text.data()), text.size()};
}

inline std::string EscapeLoadInfoPath(std::string_view path) {
    std::string out;
    for (const char c : path) {
        switch (c) {
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        default: out += c; break;
        }
    }
    return out;
}

inline std::string_view LoadInfoKind(dict::LoadFileKind kind) {
    using Kind = dict::LoadFileKind;
    switch (kind) {
    case Kind::Shipped: return "기본 사전";
    case Kind::NativeUser: return "사용자 사전";
    case Kind::NativePreFilter: return "전처리 필터";
    case Kind::NativePostFilter: return "후처리 필터";
    case Kind::EhndPreFilter: return "Ehnd 전처리 필터";
    case Kind::EhndPostFilter: return "Ehnd 후처리 필터";
    case Kind::EhndSkipLayer: return "Ehnd 레이어 조건";
    case Kind::EhndUserDict: return "Ehnd 사용자 사전";
    case Kind::CompiledSnapshot: return "기본 사전, 컴파일";
    case Kind::CachedDictionary: return "기본 사전, 캐시";
    case Kind::MemoryUser: return "사용자 사전, 메모리";
    case Kind::MemoryEhndPreFilter: return "Ehnd 전처리 필터, 메모리";
    case Kind::MemoryEhndPostFilter: return "Ehnd 후처리 필터, 메모리";
    case Kind::MemoryEhndSkipLayer: return "Ehnd 레이어 조건, 메모리";
    case Kind::MemoryEhndUserDict: return "Ehnd 사용자 사전, 메모리";
    case Kind::HostUserDict: return "아네모네 사용자 사전";
    case Kind::EhndDisabled: return {};
    }
    return {};
}

}  // namespace internal

inline std::string FormatLoadFileInfo(const dict::LoadFileInfo& info, std::string_view programDir) {
    if (info.kind == dict::LoadFileKind::EhndDisabled)
        return "Ehnd 활성화가 꺼져 있어 필터 및 사전 불러오기를 건너뜁니다";
    std::string out = "사전 읽음: ";
    if (!info.path.empty()) out += internal::EscapeLoadInfoPath(internal::LoadInfoPath(info.path, programDir)) + ' ';
    out += '(';
    out += internal::LoadInfoKind(info.kind);
    if (info.acceptedCount) out += ", " + std::to_string(*info.acceptedCount) + "개";
    out += ')';
    return out;
}

}  // namespace hdTrans::frontend
