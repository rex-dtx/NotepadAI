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

#include "SshDebugDialog.h"

#include "remote/SshConnection.h"

#include <QApplication>
#include <QClipboard>
#include <QFont>
#include <QHBoxLayout>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollBar>
#include <QVBoxLayout>

SshDebugDialog::SshDebugDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(tr("SSH Debug Log"));
    resize(800, 500);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(8, 8, 8, 8);

    m_text = new QPlainTextEdit(this);
    m_text->setReadOnly(true);
    m_text->setLineWrapMode(QPlainTextEdit::NoWrap);
    QFont mono = m_text->font();
    mono.setFamily(QStringLiteral("Consolas"));
    mono.setStyleHint(QFont::Monospace);
    m_text->setFont(mono);
    layout->addWidget(m_text, 1);

    auto *btnRow = new QHBoxLayout();
    auto *refreshBtn = new QPushButton(tr("Refresh"), this);
    auto *copyBtn    = new QPushButton(tr("Copy all"), this);
    auto *clearBtn   = new QPushButton(tr("Clear buffer"), this);
    auto *closeBtn   = new QPushButton(tr("Close"), this);
    btnRow->addWidget(refreshBtn);
    btnRow->addWidget(copyBtn);
    btnRow->addWidget(clearBtn);
    btnRow->addStretch();
    btnRow->addWidget(closeBtn);
    layout->addLayout(btnRow);

    connect(refreshBtn, &QPushButton::clicked, this, &SshDebugDialog::onRefresh);
    connect(copyBtn,    &QPushButton::clicked, this, &SshDebugDialog::onCopy);
    connect(clearBtn,   &QPushButton::clicked, this, &SshDebugDialog::onClear);
    connect(closeBtn,   &QPushButton::clicked, this, &QDialog::hide);
}

void SshDebugDialog::bindToConnection(remote::SshConnection *conn)
{
    // Disconnect old live-tail (wired with `this` as context).
    if (m_connection)
        disconnect(m_connection, &remote::SshConnection::debugLogAppended, this, nullptr);

    m_connection = conn;

    if (m_connection) {
        // Populate from the existing ring buffer snapshot.
        m_text->setPlainText(m_connection->debugLog().join(QLatin1Char('\n')));
        QScrollBar *bar = m_text->verticalScrollBar();
        if (bar) bar->setValue(bar->maximum());

        // Wire live tail. QPointer guard drops any already-queued signals that
        // arrive after the connection is destroyed (QPointer is nulled first
        // inside ~QObject, before the event loop can dispatch queued deliveries).
        connect(m_connection, &remote::SshConnection::debugLogAppended, this,
                [this](const QString &line) {
                    if (!m_connection) return;
                    QScrollBar *sb = m_text->verticalScrollBar();
                    const bool wasAtBottom = sb ? (sb->value() == sb->maximum()) : true;
                    m_text->appendPlainText(line);
                    if (wasAtBottom && sb) sb->setValue(sb->maximum());
                });
    } else {
        m_text->setPlainText(tr("(no active SSH workspace)"));
    }
}

void SshDebugDialog::onRefresh()
{
    if (!m_connection) {
        m_text->setPlainText(tr("(no active SSH workspace)"));
        return;
    }
    m_text->setPlainText(m_connection->debugLog().join(QLatin1Char('\n')));
    QScrollBar *bar = m_text->verticalScrollBar();
    if (bar) bar->setValue(bar->maximum());
}

void SshDebugDialog::onCopy() const
{
    QGuiApplication::clipboard()->setText(m_text->toPlainText());
}

void SshDebugDialog::onClear()
{
    if (m_connection)
        m_connection->clearDebugLog();
    m_text->clear();
}
