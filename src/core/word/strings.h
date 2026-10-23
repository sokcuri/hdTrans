#pragma once
// Japanese and Korean texts the candidate checks compare against.
#include <array>
#include <string_view>

namespace hdTrans::word {

inline constexpr std::string_view kJaA = "あ";
inline constexpr std::string_view kJaAiKanji = "合";
inline constexpr std::string_view kJaAsa = "あさ";
inline constexpr std::string_view kJaAsaKanji = "漁";
inline constexpr std::string_view kJaBi = "び";
inline constexpr std::string_view kJaChira = "ちら";
inline constexpr std::string_view kJaChiraKanji = "散ら";
inline constexpr std::string_view kJaDa = "だ";
inline constexpr std::string_view kJaDe = "で";
inline constexpr std::string_view kJaDzura = "づら";
inline constexpr std::string_view kJaGa = "が";
inline constexpr std::string_view kJaGachi = "がち";
inline constexpr std::string_view kJaGara = "がら";
inline constexpr std::string_view kJaGata = "がた";
inline constexpr std::string_view kJaGataKanji = "難";
inline constexpr std::string_view kJaHa = "は";
inline constexpr std::string_view kJaHaji = "はじ";
inline constexpr std::string_view kJaHajiKanji = "始";
inline constexpr std::string_view kJaHe = "へ";
inline constexpr std::string_view kJaI = "い";
inline constexpr std::string_view kJaSuru = "する";
inline constexpr std::string_view kJaComma = "、";
inline constexpr std::string_view kJaFullComma = "，";
inline constexpr std::string_view kJaIke = "いけ";
inline constexpr std::string_view kJaKa = "か";
inline constexpr std::string_view kJaKake = "かけ";
inline constexpr std::string_view kJaKane = "かね";
inline constexpr std::string_view kJaKara = "から";
inline constexpr std::string_view kJaKat = "かっ";
inline constexpr std::string_view kJaKawa = "かわ";
inline constexpr std::string_view kJaKawaKanji = "交わ";
inline constexpr std::string_view kJaKerepa = "けれぱ";
inline constexpr std::string_view kJaKire = "きれ";
inline constexpr std::string_view kJaKireKanji = "切れ";
inline constexpr std::string_view kJaKudasa = "くださ";
inline constexpr std::string_view kJaKudasaKanji = "下さ";
inline constexpr std::string_view kJaMa = "ま";
inline constexpr std::string_view kJaMade = "まで";
inline constexpr std::string_view kJaMase = "ませ";
inline constexpr std::string_view kJaMashi = "まし";
inline constexpr std::string_view kJaMasu = "ます";
inline constexpr std::string_view kJaMiKanji = "見";
inline constexpr std::string_view kJaMitai = "みたい";
inline constexpr std::string_view kJaMo = "も";
inline constexpr std::string_view kJaN = "ん";
inline constexpr std::string_view kJaNa = "な";
inline constexpr std::string_view kJaRare = "られ";
inline constexpr std::string_view kJaSare = "され";
inline constexpr std::string_view kJaNado = "など";
inline constexpr std::string_view kJaNagara = "ながら";
inline constexpr std::string_view kJaNao = "なお";
inline constexpr std::string_view kJaNaoKanji = "直";
inline constexpr std::string_view kJaNasai = "なさい";
inline constexpr std::string_view kJaNazo = "なぞ";
inline constexpr std::string_view kJaNe = "ね";
inline constexpr std::string_view kJaNeba = "ねば";
inline constexpr std::string_view kJaNi = "に";
inline constexpr std::string_view kJaNiku = "にく";
inline constexpr std::string_view kJaNna = "んな";
inline constexpr std::string_view kJaNo = "の";
inline constexpr std::string_view kJaNokata = "のかた";
inline constexpr std::string_view kJaNokataKanji = "の方";
inline constexpr std::string_view kJaNu = "ぬ";
inline constexpr std::string_view kJaO = "お";
inline constexpr std::string_view kJaOki = "おき";
inline constexpr std::string_view kJaOyo = "およ";
inline constexpr std::string_view kJaQuestion = "？";
inline constexpr std::string_view kJaRashi = "らし";
inline constexpr std::string_view kJaRe = "れ";
inline constexpr std::string_view kJaSae = "さえ";
inline constexpr std::string_view kJaSa = "さ";
inline constexpr std::string_view kJaSaSugi = "さすぎ";
inline constexpr std::string_view kJaSaSugiKanji = "さ過ぎ";
inline constexpr std::string_view kJaShi = "し";
inline constexpr std::string_view kJaShika = "しか";
inline constexpr std::string_view kJaShitai = "したい";
inline constexpr std::string_view kJaShitakatta = "したかった";
inline constexpr std::string_view kJaShitakereba = "したければ";
inline constexpr std::string_view kJaShitaki = "したき";
inline constexpr std::string_view kJaShitaku = "したく";
inline constexpr std::string_view kJaSokona = "そこな";
inline constexpr std::string_view kJaSokonaKanji = "損な";
inline constexpr std::string_view kJaSou = "そう";
inline constexpr std::string_view kJaSugi = "すぎ";
inline constexpr std::string_view kJaSugiKanji = "過ぎ";
inline constexpr std::string_view kJaTabeKanji = "食べ";
inline constexpr std::string_view kJaTai = "たい";
inline constexpr std::string_view kJaTakatta = "たかった";
inline constexpr std::string_view kJaTakereba = "たければ";
inline constexpr std::string_view kJaTaki = "たき";
inline constexpr std::string_view kJaTaku = "たく";
inline constexpr std::string_view kJaTakuseni = "たくせに";
inline constexpr std::string_view kJaTakurai = "たくらい";
inline constexpr std::string_view kJaKurai = "くらい";
inline constexpr std::string_view kJaKuseni = "くせに";
inline constexpr std::string_view kJaTanomuKanji = "頼";
inline constexpr std::string_view kJaTe = "て";
inline constexpr std::string_view kJaTo = "と";
inline constexpr std::string_view kJaTsudzu = "つづ";
inline constexpr std::string_view kJaTsudzuKanji = "続";
inline constexpr std::string_view kJaTsudzuke = "つづけ";
inline constexpr std::string_view kJaTsudzukeKanji = "続け";
inline constexpr std::string_view kJaTsuku = "つく";
inline constexpr std::string_view kJaTsukuKanji = "尽く";
inline constexpr std::string_view kJaTsutsu = "つつ";
inline constexpr std::string_view kJaTsutsua = "つつあ";
inline constexpr std::string_view kJaUru = "うる";
inline constexpr std::string_view kJaWo = "を";
inline constexpr std::string_view kJaYa = "や";
inline constexpr std::string_view kJaYaga = "やが";
inline constexpr std::string_view kJaYasu = "やす";
inline constexpr std::string_view kJaYasuKanji = "安";
inline constexpr std::string_view kJaYo = "よ";
inline constexpr std::string_view kJaYou = "よう";
inline constexpr std::string_view kJaZu = "ず";
inline constexpr std::string_view kJaZaru = "ざる";
inline constexpr std::string_view kJaOwaru = "終わる";
inline constexpr std::string_view kJaGyou = "行";
inline constexpr std::string_view kJaEllipsis = "…";
inline constexpr std::string_view kJaTwoDots = "‥";
inline constexpr std::string_view kKoCloseQuote = "」";
inline constexpr std::string_view kKoIGa = "(이)가";
inline constexpr std::string_view kKoDeon = "던";
inline constexpr std::string_view kKoAn = "안";
inline constexpr std::string_view kKoGalSu = "갈 수";
inline constexpr std::string_view kKoIn = "인";
inline constexpr std::string_view kKoHan = "한";
inline constexpr std::string_view kKoYo = "요";
inline constexpr std::string_view kKoBogo = "보고";
inline constexpr std::string_view kKoJi = "지";
inline constexpr std::string_view kKoAnh = "않";
inline constexpr std::string_view kKoDa = "다";
inline constexpr std::string_view kKoMyeon = "면";
inline constexpr std::string_view kKoReo = "러";
inline constexpr std::string_view kKoRyeo = "려";
inline constexpr std::string_view kKoGeot = "것";
inline constexpr std::string_view kNumeralDigits = "○一二三四五六七八九";
inline constexpr std::string_view kNumeralCircle = "〇";
inline constexpr std::string_view kNumeralOrder = "兆億万万千百十九八七六六五四三二一○";

// お, ご, 御.
inline constexpr std::array<std::string_view, 3> kHonorificPrefixes = {"お", "ご", "御"};

// The auxiliaries that start after a て form.
inline constexpr std::string_view kTeAuxiliaries[] = {"い", "居", "お", "置", "く", "下さ", "来", "き", "み", "見", "も", "貰",
                                                      "しま", "仕舞", "ほし", "欲し", "あ", "や", "いただ", "頂", "差し",
                                                      "行", "ちょうだい"};

// The stems of the polite request verbs.
inline constexpr std::array<std::string_view, 6> kRequestStems = {"くださ", "下さ", "いらっしゃ", "おっしゃ", "なさ", "ござ"};

}  // namespace hdTrans::word
