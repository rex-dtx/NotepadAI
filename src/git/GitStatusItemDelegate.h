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

#ifndef GIT_STATUS_ITEM_DELEGATE_H
#define GIT_STATUS_ITEM_DELEGATE_H

#include <QStyledItemDelegate>

// Paints status rows with three segments in one pass:
//   1) "<X>  <name>"   — change-colored
//   2) "  -N"          — minus-colored
//   3) " +N"           — plus-colored
//
// Section rows fall through to the base class so the framed group title still
// renders the way the theme expects.
class GitStatusItemDelegate : public QStyledItemDelegate
{
    Q_OBJECT
public:
    explicit GitStatusItemDelegate(QObject *parent = nullptr);

    void setDarkPalette(bool dark);

    void paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

private:
    bool m_isDark = false;
};

#endif // GIT_STATUS_ITEM_DELEGATE_H
