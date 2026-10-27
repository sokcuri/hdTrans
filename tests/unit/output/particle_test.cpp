#include "core/output/particle.h"

#include <string>

#include "hdtest.h"

using hdTrans::output::ParticleClass;
using hdTrans::output::internal::LatinWordClass;

namespace {

std::string Show(char c) { return c == 0 ? "0" : std::string(1, c); }

}  // namespace

// The final sound of a Latin word by the Korean reading of its English spelling.
HD_TEST(output_latin_word_class) {
    struct Case {
        const char* word;
        char want;
    };
    const Case cases[] = {
        {"Sam", 'c'}, {"Adam", 'c'}, {"Liam", 'c'}, {"Pam", 'c'}, {"Room", 'c'}, {"Mom", 'c'}, {"Team", 'c'},
        {"Program", 'c'}, {"Spam", 'c'}, {"Dream", 'c'}, {"Zoom", 'c'}, {"Museum", 'c'}, {"Premium", 'c'},
        {"Ken", 'c'}, {"Ben", 'c'}, {"Green", 'c'}, {"Queen", 'c'}, {"Moon", 'c'}, {"Golden", 'c'},
        {"Token", 'c'}, {"Kevin", 'c'}, {"Sean", 'c'}, {"Allen", 'c'}, {"Helen", 'c'}, {"Station", 'c'},
        {"Login", 'c'}, {"Admin", 'c'}, {"Plugin", 'c'}, {"Mountain", 'c'}, {"Captain", 'c'}, {"Bean", 'c'},
        {"Paul", 'l'}, {"Tool", 'l'}, {"Apple", 'l'}, {"Cat", 'c'}, {"John", 'c'}, {"Dan", 'c'}, {"Ann", 'c'},
        {"Brown", 'c'}, {"Bob", 'c'}, {"Jack", 'c'}, {"Book", 'c'}, {"Nick", 'c'}, {"Rock", 'c'}, {"Bill", 'l'},
        {"Will", 'l'}, {"Ring", 'c'}, {"King", 'c'}, {"Song", 'c'}, {"Mark", 'v'}, {"Mike", 'v'},
        {"Steak", 'v'}, {"Dog", 'v'}, {"Bus", 'v'}, {"Max", 'v'}, {"Jeff", 'v'}, {"Chris", 'v'}, {"Tom", 'c'},
        {"Mary", 'v'}, {"Google", 'l'}, {"Nelson", 'c'}, {"girl", 'l'}, {"Morning", 'c'}, {"Hi", 'v'},
        {"game", 'c'}, {"time", 'c'}, {"home", 'c'}, {"name", 'c'}, {"line", 'c'}, {"phone", 'c'},
        {"iPhone", 'c'}, {"zone", 'c'}, {"stone", 'c'}, {"online", 'c'}, {"engine", 'c'}, {"table", 'l'},
        {"title", 'l'}, {"style", 'l'}, {"file", 'l'}, {"smile", 'l'}, {"mobile", 'l'}, {"puzzle", 'l'},
        {"castle", 'l'}, {"circle", 'l'}, {"fire", 'v'}, {"core", 'v'}, {"store", 'v'}, {"case", 'v'},
        {"house", 'v'}, {"race", 'v'}, {"site", 'v'}, {"note", 'v'}, {"late", 'v'}, {"cake", 'v'},
        {"make", 'v'}, {"code", 'v'}, {"page", 'v'}, {"image", 'v'}, {"rope", 'v'}, {"live", 'v'},
        {"tube", 'v'}, {"YouTube", 'v'}, {"size", 'v'}, {"tree", 'v'}, {"coffee", 'v'}, {"blue", 'v'},
        {"value", 'v'}, {"movie", 'v'}, {"league", 'v'}, {"unique", 'v'}, {"map", 'c'}, {"top", 'c'},
        {"cup", 'c'}, {"shop", 'c'}, {"tip", 'c'}, {"app", 'c'}, {"step", 'c'}, {"chip", 'c'}, {"lamp", 'v'},
        {"help", 'v'}, {"jump", 'v'}, {"cut", 'c'}, {"shot", 'c'}, {"hot", 'c'}, {"robot", 'c'},
        {"ticket", 'c'}, {"rocket", 'c'}, {"pocket", 'c'}, {"market", 'c'}, {"target", 'c'}, {"secret", 'c'},
        {"helmet", 'c'}, {"tablet", 'c'}, {"Matt", 'c'}, {"Scott", 'c'}, {"part", 'v'}, {"desk", 'v'},
        {"mask", 'v'}, {"test", 'v'}, {"best", 'v'}, {"act", 'v'}, {"script", 'v'}, {"JavaScript", 'v'},
        {"print", 'v'}, {"park", 'v'}, {"work", 'v'}, {"pink", 'v'}, {"bank", 'v'}, {"link", 'v'},
        {"think", 'v'}, {"milk", 'v'}, {"meat", 'v'}, {"sheet", 'v'}, {"boat", 'v'}, {"coat", 'v'},
        {"wait", 'v'}, {"suit", 'v'}, {"great", 'v'}, {"week", 'v'}, {"speak", 'v'}, {"peak", 'v'},
        {"look", 'c'}, {"cook", 'c'}, {"Facebook", 'c'}, {"magic", 'c'}, {"music", 'c'}, {"logic", 'c'},
        {"comic", 'c'}, {"Mac", 'c'}, {"disc", 'v'}, {"web", 'c'}, {"club", 'c'}, {"pub", 'c'}, {"job", 'c'},
        {"tab", 'c'}, {"lab", 'c'}, {"bomb", 'c'}, {"climb", 'c'}, {"lamb", 'c'}, {"bulb", 'v'}, {"bed", 'v'},
        {"road", 'v'}, {"card", 'v'}, {"kid", 'v'}, {"red", 'v'}, {"bread", 'v'}, {"dog", 'v'}, {"bug", 'v'},
        {"log", 'v'}, {"frog", 'v'}, {"egg", 'v'}, {"tag", 'v'}, {"flag", 'v'}, {"bag", 'c'}, {"handbag", 'c'}, {"big", 'c'}, {"long", 'c'}, {"song", 'c'}, {"thing", 'c'}, {"sign", 'c'},
        {"design", 'c'}, {"campaign", 'c'}, {"high", 'v'}, {"night", 'v'}, {"light", 'v'}, {"bus", 'v'},
        {"class", 'v'}, {"gas", 'v'}, {"James", 'v'}, {"games", 'v'}, {"box", 'v'}, {"fox", 'v'}, {"quiz", 'v'},
        {"chef", 'v'}, {"golf", 'v'}, {"cash", 'v'}, {"flash", 'v'}, {"English", 'v'}, {"catch", 'v'},
        {"match", 'v'}, {"lunch", 'v'}, {"coach", 'v'}, {"tech", 'v'}, {"graph", 'v'}, {"Smith", 'v'},
        {"bath", 'v'}, {"month", 'v'}, {"Sarah", 'v'}, {"Noah", 'v'}, {"car", 'v'}, {"computer", 'v'},
        {"color", 'v'}, {"star", 'v'}, {"show", 'v'}, {"window", 'v'}, {"new", 'v'}, {"happy", 'v'},
        {"boy", 'v'}, {"day", 'v'}, {"key", 'v'}, {"Tokyo", 'v'}, {"pizza", 'v'}, {"radio", 'v'}, {"menu", 'v'},
        {"ball", 'l'}, {"call", 'l'}, {"cell", 'l'}, {"skill", 'l'}, {"doll", 'l'}, {"cool", 'l'},
        {"mail", 'l'}, {"real", 'l'}, {"hotel", 'l'}, {"level", 'l'}, {"model", 'l'}, {"angel", 'l'},
        {"email", 'l'}, {"Michael", 'l'}, {"Daniel", 'l'}, {"Seoul", 'l'}, {"Mohawk", 'v'}, {"Owen", 'c'},
        {"silhouette", 'c'}, {"omelette", 'c'}, {"roulette", 'c'}, {"Juliette", 'c'}, {"Me", 'v'}, {"We", 'v'},
        {"Ok", 'v'}, {"watt", 'c'}, {"ChatGPT", 'v'}, {"Boston", 'c'}, {"Encore", 'v'}, {"Voiture", 'v'},
        {"Bobcat", 'c'}, {"www", 'v'}, {"x", 'v'}, {"Abc3", 'v'},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Show(LatinWordClass(c.word)) + " " + c.word, Show(c.want) + " " + c.word);
}

// Only the last letters of a long word are read, and only the letters.
HD_TEST(output_latin_word_class_window) {
    HD_CHECK_EQ(Show(LatinWordClass("Supercalifragilisticexpialidocious")), Show('v'));
    HD_CHECK_EQ(Show(LatinWordClass("Internationalization")), Show('c'));
    HD_CHECK_EQ(Show(LatinWordClass("xxxxxxxxxxxxcat")), Show('c'));
    HD_CHECK_EQ(Show(LatinWordClass("3.Tom")), Show('c'));
    HD_CHECK_EQ(Show(LatinWordClass("")), Show('v'));
}

// A word in capitals or a single letter reads by its letter names, a unit after a number
// by its reading, another word by its spelling.
HD_TEST(output_particle_class_latin) {
    struct Case {
        const char* in;
        char want;
    };
    const Case cases[] = {
        {"Tom", 'c'}, {"Paul", 'l'}, {"Apple", 'l'}, {"Mary", 'v'}, {"Cat", 'c'}, {"Ｔｏｍ", 'c'},
        {"TOM", 'c'}, {"API", 'v'}, {"BGM", 'c'}, {"RPG", 'v'}, {"m", 'c'}, {"5km", 'v'},
        {"「Tom」", 'c'}, {"Station", 'c'}, {"Mike", 'v'},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Show(ParticleClass(c.in)) + " " + c.in, Show(c.want) + " " + c.in);
}

// A unit, currency or letter sign reads by its Korean reading (℃ 도, ㎏ 킬로그램, ‰ 퍼밀, ③ 삼).
HD_TEST(output_particle_class_signs) {
    struct Case {
        const char* in;
        char want;
    };
    const Case cases[] = {
        {"100℃", 'v'}, {"3㎞", 'v'}, {"10€", 'v'}, {"100$", 'v'}, {"5㎏", 'c'}, {"1000₩", 'c'},
        {"5‰", 'l'},   {"①", 'l'},  {"②", 'v'},  {"③", 'c'},    {"α", 'v'},  {"ε", 'c'},
    };
    for (const auto& c : cases) HD_CHECK_EQ(Show(ParticleClass(c.in)) + " " + c.in, Show(c.want) + " " + c.in);
}
