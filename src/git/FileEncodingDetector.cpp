/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "FileEncodingDetector.h"

#include "uchardet.h"

#include <QTextCodec>

#include <cstdint>

FileEncodingDetector::Bom FileEncodingDetector::detectBom(const QByteArray &raw)
{
    const auto *p = reinterpret_cast<const unsigned char *>(raw.constData());
    const qsizetype n = raw.size();
    if (n >= 3 && p[0] == 0xEF && p[1] == 0xBB && p[2] == 0xBF) return Bom::Utf8;
    if (n >= 2 && p[0] == 0xFF && p[1] == 0xFE)                 return Bom::Utf16LE;
    if (n >= 2 && p[0] == 0xFE && p[1] == 0xFF)                 return Bom::Utf16BE;
    return Bom::None;
}

bool FileEncodingDetector::isValidUtf8(const char *data, qsizetype size)
{
    // Hot loop: walk once, branch-light. Reject overlongs and surrogates.
    const auto *p = reinterpret_cast<const unsigned char *>(data);
    const unsigned char *end = p + size;
    while (p < end) {
        const unsigned char c = *p;
        if (c < 0x80) { ++p; continue; }

        int need;
        std::uint32_t cp;
        if      ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; if (c < 0xC2) return false; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; if (c > 0xF4) return false; }
        else return false;

        if (end - p <= need) return false;
        for (int i = 1; i <= need; ++i) {
            const unsigned char cc = p[i];
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3F);
        }
        // Overlong / surrogate / out-of-range
        if (need == 1 && cp < 0x80)            return false;
        if (need == 2 && cp < 0x800)           return false;
        if (need == 3 && cp < 0x10000)         return false;
        if (cp >= 0xD800 && cp <= 0xDFFF)      return false;
        if (cp > 0x10FFFF)                      return false;
        p += need + 1;
    }
    return true;
}

bool FileEncodingDetector::decode(const QByteArray &raw, QString &out, Bom *bomOut)
{
    const Bom bom = detectBom(raw);
    if (bomOut) *bomOut = bom;

    switch (bom) {
    case Bom::Utf8:
        out = QString::fromUtf8(raw.constData() + 3, raw.size() - 3);
        return true;
    case Bom::Utf16LE: {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16LE");
        out = codec ? codec->toUnicode(raw.constData() + 2, raw.size() - 2)
                    : QString::fromUtf16(reinterpret_cast<const char16_t *>(raw.constData() + 2),
                                         (raw.size() - 2) / 2);
        return true;
    }
    case Bom::Utf16BE: {
        QTextCodec *codec = QTextCodec::codecForName("UTF-16BE");
        if (codec) {
            out = codec->toUnicode(raw.constData() + 2, raw.size() - 2);
            return true;
        }
        return false;
    }
    case Bom::None:
        break;
    }

    // No BOM. Fast path: try UTF-8 strict validation. Covers ~95% of source.
    if (isValidUtf8(raw.constData(), raw.size())) {
        out = QString::fromUtf8(raw);
        return true;
    }

    // Fallback: uchardet on the head (sniff ~64 KiB for budget control).
    uchardet_t ud = uchardet_new();
    const qsizetype sniff = qMin<qsizetype>(raw.size(), 64 * 1024);
    if (uchardet_handle_data(ud, raw.constData(), static_cast<size_t>(sniff)) != 0) {
        uchardet_delete(ud);
        out = QString::fromLatin1(raw);
        return false;
    }
    uchardet_data_end(ud);
    const char *enc = uchardet_get_charset(ud);
    QString result;
    bool ok = false;
    if (enc && enc[0] != '\0') {
        if (QTextCodec *codec = QTextCodec::codecForName(enc)) {
            QTextCodec::ConverterState st;
            result = codec->toUnicode(raw.constData(), raw.size(), &st);
            ok = (st.invalidChars == 0);
        }
    }
    uchardet_delete(ud);
    if (ok) {
        out = result;
        return true;
    }
    out = QString::fromLatin1(raw);
    return false;
}
