/**
 * Created by ROSHii on 2019-06-01.
 * Copyright (c) 2013-2014 Tomas Dzetkulic
 * Copyright (c) 2013-2014 Pavol Rusnak
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES
 * OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */


#include "wallet/bip39.h"
#include "crypto/sha256.h"
#include "random.h"
#include "wallet/bip39_chinese_simplified.h"
#include "wallet/bip39_chinese_traditional.h"
#include "wallet/bip39_english.h"
#include "wallet/bip39_french.h"
#include "wallet/bip39_italian.h"
#include "wallet/bip39_japanese.h"
#include "wallet/bip39_korean.h"
#include "wallet/bip39_spanish.h"

#include <openssl/evp.h>

SecureString CMnemonic::Generate(int strength, int languageSelected)
{
    if (strength % 32 || strength < 128 || strength > 256) {
        return SecureString();
    }
    SecureVector data(32);
    GetStrongRandBytes(&data[0], 32);
    SecureString mnemonic = FromData(data, strength / 8, languageSelected);
    return mnemonic;
}

// SecureString CMnemonic::FromData(const uint8_t *data, int len)
SecureString CMnemonic::FromData(const SecureVector& data, int len, int languageSelected)
{
    if (len % 4 || len < 16 || len > 32) {
        return SecureString();
    }

    SecureVector checksum(32);
    CSHA256().Write(&data[0], len).Finalize(&checksum[0]);

    // data
    SecureVector bits(len);
    memcpy(&bits[0], &data[0], len);
    // checksum
    bits.push_back(checksum[0]);

    int mlen = len * 3 / 4;
    SecureString mnemonic;

    const char* const* wordlist;
    wordlist = CMnemonic::GetLanguageWords(languageSelected);

    int i, j, idx;
    for (i = 0; i < mlen; i++) {
        idx = 0;
        for (j = 0; j < 11; j++) {
            idx <<= 1;
            idx += (bits[(i * 11 + j) / 8] & (1 << (7 - ((i * 11 + j) % 8)))) > 0;
        }
        mnemonic.append(wordlist[idx]);
        if (i < mlen - 1) {
            mnemonic += ' ';
        }
    }

    return mnemonic;
}

bool CMnemonic::Check(SecureString mnemonic, int languageSelected)
{
    if (mnemonic.empty()) {
        return false;
    }

    uint32_t nWordCount{};

    for (size_t i = 0; i < mnemonic.size(); ++i) {
        if (mnemonic[i] == ' ') {
            nWordCount++;
        }
    }
    nWordCount++;

    // check number of words
    if (nWordCount != 12 && nWordCount != 18 && nWordCount != 24) {
        return false;
    }

    SecureString ssCurrentWord;
    SecureVector bits(32 + 1);

    if (languageSelected == -1) {
        languageSelected = CMnemonic::DetectLanguageSeed(mnemonic);
    }

    const char* const* wordlist;
    wordlist = CMnemonic::GetLanguageWords(languageSelected);

    uint32_t nWordIndex, ki, nBitsCount{};

    for (size_t i = 0; i < mnemonic.size(); ++i) {
        ssCurrentWord = "";
        while (i + ssCurrentWord.size() < mnemonic.size() && mnemonic[i + ssCurrentWord.size()] != ' ') {
            ssCurrentWord += mnemonic[i + ssCurrentWord.size()];
        }
        i += ssCurrentWord.size();
        nWordIndex = 0;
        for (;;) {
            if (!wordlist[nWordIndex]) { // word not found
                return false;
            }
            if (ssCurrentWord == wordlist[nWordIndex]) { // word found on index nWordIndex
                for (ki = 0; ki < 11; ki++) {
                    if (nWordIndex & (1 << (10 - ki))) {
                        bits[nBitsCount / 8] |= 1 << (7 - (nBitsCount % 8));
                    }
                    nBitsCount++;
                }
                break;
            }
            nWordIndex++;
        }
    }
    if (nBitsCount != nWordCount * 11) {
        return false;
    }
    bits[32] = bits[nWordCount * 4 / 3];
    CSHA256().Write(&bits[0], nWordCount * 4 / 3).Finalize(&bits[0]);

    bool fResult = 0;
    if (nWordCount == 12) {
        fResult = (bits[0] & 0xF0) == (bits[32] & 0xF0); // compare first 4 bits
    } else if (nWordCount == 18) {
        fResult = (bits[0] & 0xFC) == (bits[32] & 0xFC); // compare first 6 bits
    } else if (nWordCount == 24) {
        fResult = bits[0] == bits[32]; // compare 8 bits
    }

    return fResult;
}

std::array<LanguageDetails, NUM_LANGUAGES_BIP39_SUPPORTED> CMnemonic::GetLanguagesDetails()
{
    return {{
        {"English", ENGLISH, 12, wordlist_en},
        {"Spanish", SPANISH, 3, wordlist_es},
        {"French", FRENCH, 12, wordlist_fr},
        {"Japanese", JAPANESE, 3, wordlist_ja},
        {"Chinese Simplified", CHINESE_SIMPLIFIED, 12, wordlist_zh_s},
        {"Chinese Traditional", CHINESE_TRADITIONAL, 12, wordlist_zh_t},
        {"Korean", KOREAN, 3, wordlist_ko},
        {"Italian", ITALIAN, 3, wordlist_it}
    }};
}

const char* const* CMnemonic::GetLanguageWords(int lang)
{
    if (lang >= 0 && lang <= NUM_LANGUAGES_BIP39_SUPPORTED) {
        return CMnemonic::GetLanguagesDetails()[lang].wordlist;
    }

    return CMnemonic::GetLanguagesDetails()[DEFAULT_LANG].wordlist;
}

int CMnemonic::DetectLanguageSeed(SecureString mnemonic)
{
    SecureString ssCurrentWord;
    uint32_t nWordIndex;

    int lang_detected = -1;

    for (int lang = 0; lang <= 7 && lang_detected == -1; lang++) {
        const char* const* wordlist;
        wordlist = CMnemonic::GetLanguageWords(lang);

        int words_founds = 0;

        int required_words_to_detect = CMnemonic::GetLanguagesDetails()[lang].minimumWordsCheckLang;
        int words_readed = 0;

        bool searching_is_ok = true;
        for (size_t i = 0; i < mnemonic.size() && words_founds < required_words_to_detect && searching_is_ok; ++i) {
            ssCurrentWord = "";
            while (i + ssCurrentWord.size() < mnemonic.size() && mnemonic[i + ssCurrentWord.size()] != ' ') {
                ssCurrentWord += mnemonic[i + ssCurrentWord.size()];
            }

            i += ssCurrentWord.size();
            words_readed++;

            for (nWordIndex = 0; nWordIndex < 2048; nWordIndex++) {
                if (ssCurrentWord == wordlist[nWordIndex]) { // word found on index nWordIndex
                    words_founds++;
                }

                if (words_founds == required_words_to_detect) {
                    lang_detected = lang;
                }
            }

            if (words_founds != words_readed) {
                searching_is_ok = false;
            }
        }
    }

    return lang_detected;
}

void CMnemonic::ToSeed(SecureString mnemonic, SecureString passphrase, SecureVector& seedRet)
{
    SecureString ssSalt = SecureString("mnemonic") + passphrase;
    SecureVector vchSalt(ssSalt.begin(), ssSalt.end());
    seedRet.resize(64);
    PKCS5_PBKDF2_HMAC(mnemonic.c_str(), mnemonic.size(), &vchSalt[0], vchSalt.size(), 2048, EVP_sha512(), 64, &seedRet[0]);
}