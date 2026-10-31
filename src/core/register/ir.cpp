#include "core/register/ir.h"

namespace hdTrans::reg {

const char* Name(Level level) {
    switch (level) {
        case Level::haera: return "haera";
        case Level::hae: return "hae";
        case Level::haeyo: return "haeyo";
        case Level::hapsyo: return "hapsyo";
        case Level::hao: return "hao";
        case Level::hage: return "hage";
        default: return "none";
    }
}

const char* Name(Persona persona) {
    switch (persona) {
        case Persona::rough: return "rough";
        case Persona::oldMale: return "old-male";
        case Persona::lady: return "lady";
        case Persona::formal: return "formal";
        case Persona::pompous: return "pompous";
        case Persona::archaic: return "archaic";
        case Persona::grand: return "grand";
        default: return "none";
    }
}

bool ParseLevel(std::string_view s, Level& out) {
    for (const Level l : {Level::none, Level::haera, Level::hae, Level::haeyo, Level::hapsyo, Level::hao, Level::hage})
        if (s == Name(l)) {
            out = l;
            return true;
        }
    return false;
}

bool ParsePersona(std::string_view s, Persona& out) {
    for (const Persona p : {Persona::none, Persona::rough, Persona::oldMale, Persona::lady, Persona::formal,
                            Persona::pompous, Persona::archaic, Persona::grand})
        if (s == Name(p)) {
            out = p;
            return true;
        }
    return false;
}

}  // namespace hdTrans::reg
