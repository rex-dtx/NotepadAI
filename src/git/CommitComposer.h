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

#ifndef COMMIT_COMPOSER_H
#define COMMIT_COMPOSER_H

#include <QPlainTextEdit>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;

class CommitMessageEdit : public QPlainTextEdit
{
    Q_OBJECT
public:
    explicit CommitMessageEdit(QWidget *parent = nullptr);
protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
signals:
    void submitRequested();
};

class CommitComposer : public QWidget
{
    Q_OBJECT
public:
    explicit CommitComposer(QWidget *parent = nullptr);

    QString message() const;
    void setMessage(const QString &);
    bool amendChecked() const;
    bool signoffChecked() const;
    bool trackedOnly() const;
    void setAmendChecked(bool);
    void setSignoffChecked(bool);
    void setTrackedOnly(bool);
    void setSubmitEnabled(bool);
    void setPlaceholderText(const QString &);
    void clear();

signals:
    void messageChanged();
    void submitRequested();
    void amendToggled(bool);
    void signoffToggled(bool);
    void trackedOnlyToggled(bool);

private slots:
    void updateCharCount();

private:
    CommitMessageEdit *m_edit;
    QLabel *m_charCount;
    QCheckBox *m_amend;
    QCheckBox *m_signoff;
    QCheckBox *m_tracked;
    QPushButton *m_commitBtn;
};

#endif // COMMIT_COMPOSER_H
