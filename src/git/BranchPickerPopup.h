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

#ifndef BRANCH_PICKER_POPUP_H
#define BRANCH_PICKER_POPUP_H

#include <QStringList>
#include <QWidget>

class QLineEdit;
class QListView;
class QStandardItemModel;

class BranchPickerPopup : public QWidget
{
    Q_OBJECT
public:
    explicit BranchPickerPopup(QWidget *parent = nullptr);

    void setBranches(const QStringList &local,
                     const QStringList &remote,
                     const QString &current,
                     const QString &defaultBranch = QStringLiteral("main"));
    void popupAt(const QPoint &globalPos);

    static QString sanitizeBranchName(const QString &raw);

signals:
    // For local existing branch: name = "feature/x"
    // For remote: name = "origin/main" — caller will translate to local checkout.
    void branchSelected(const QString &name);
    void createBranchRequested(const QString &name, const QString &base);

protected:
    bool eventFilter(QObject *o, QEvent *e) override;

private slots:
    void rebuild();
    void onActivated(const QModelIndex &index);

private:
    QLineEdit *m_filter;
    QListView *m_list;
    QStandardItemModel *m_model;
    QString m_current;
    QString m_default;
    QStringList m_local;
    QStringList m_remote;
};

#endif // BRANCH_PICKER_POPUP_H
