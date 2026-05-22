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

#ifndef FILE_ENCODING_DETECTOR_H
#define FILE_ENCODING_DETECTOR_H

#include <QByteArray>
#include <QString>

// Tiny BOM-first encoding detector for the diff viewer: untracked files we need
// to display inline are usually source code, so BOM + UTF-8 validity covers
// 99% of cases. Falls back to uchardet only when bytes are clearly not UTF-8.
//
// Kept separate from ScintillaNext::readFromDisk to avoid touching the editor's
// load path — the diff view never goes through ScintillaNext file IO.
class FileEncodingDetector
{
public:
    enum class Bom : std::uint8_t { None, Utf8, Utf16LE, Utf16BE };

    // Decodes raw bytes to QString. Strips BOM. Returns false if decoding falls
    // back to lossy replacement (i.e. encoding could not be determined cleanly).
    static bool decode(const QByteArray &raw, QString &out, Bom *bomOut = nullptr);

private:
    static Bom detectBom(const QByteArray &raw);
    static bool isValidUtf8(const char *data, qsizetype size);
};

#endif // FILE_ENCODING_DETECTOR_H
