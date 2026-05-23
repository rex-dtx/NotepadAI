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

#include "CredentialStore.h"

#include "ApplicationSettings.h"

#include <QCoreApplication>
#include <QFile>
#include <QTextStream>
#include <QtGlobal>

namespace ai {

namespace {

constexpr auto kEnvKey     = "NOTEPADAI_COMMIT_API_KEY";
constexpr auto kEnvKeyFile = "NOTEPADAI_COMMIT_API_KEY_FILE";

QString readFirstLine(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return {};
    QTextStream in(&f);
    return in.readLine().trimmed();
}

} // namespace

CredentialStore::CredentialStore(QObject *parent) : QObject(parent) {}
CredentialStore::~CredentialStore() = default;

QString CredentialStore::envOverrideKey()
{
    const QByteArray raw = qgetenv(kEnvKey);
    if (!raw.isEmpty()) return QString::fromLocal8Bit(raw).trimmed();

    const QByteArray fileEnv = qgetenv(kEnvKeyFile);
    if (!fileEnv.isEmpty()) {
        const QString path = QString::fromLocal8Bit(fileEnv);
        const QString line = readFirstLine(path);
        if (!line.isEmpty()) return line;
    }
    return {};
}

bool CredentialStore::isApiKeyAvailable() const
{
    if (!envOverrideKey().isEmpty()) return true;
    // Read flag through ApplicationSettings if a global instance exists
    // (NotepadNextApplication owns one). Fall back to a transient instance
    // when called outside the app (e.g. tests without QApplication).
    if (auto *app = qobject_cast<QCoreApplication *>(QCoreApplication::instance())) {
        // Look up the ApplicationSettings child of the app to read the flag.
        for (QObject *child : app->children()) {
            if (auto *s = qobject_cast<ApplicationSettings *>(child)) {
                return s->commitMessageApiKeyConfigured();
            }
        }
    }
    ApplicationSettings transient;
    return transient.commitMessageApiKeyConfigured();
}

QString CredentialStore::retrieveApiKey(QString *errorOut) const
{
    QString override = envOverrideKey();
    if (!override.isEmpty()) return override;

    QString value;
    QString err;
    const bool ok = platformRetrieve(&value, &err);
    if (!ok && errorOut) *errorOut = err;
    return value;
}

bool CredentialStore::storeApiKey(const QString &value, QString *errorOut)
{
    if (value.isEmpty()) return clearApiKey(errorOut);

    QString err;
    const bool ok = platformStore(value, &err);
    if (!ok) {
        if (errorOut) *errorOut = err;
        return false;
    }
    // Update the synchronous flag via whichever ApplicationSettings instance
    // the app owns; fall back to a transient one so the on-disk QSettings
    // group reflects state even in tests.
    if (auto *app = QCoreApplication::instance()) {
        for (QObject *child : app->children()) {
            if (auto *s = qobject_cast<ApplicationSettings *>(child)) {
                s->setCommitMessageApiKeyConfigured(true);
                emit apiKeyConfiguredChanged(true);
                return true;
            }
        }
    }
    ApplicationSettings transient;
    transient.setCommitMessageApiKeyConfigured(true);
    emit apiKeyConfiguredChanged(true);
    return true;
}

bool CredentialStore::clearApiKey(QString *errorOut)
{
    QString err;
    const bool ok = platformClear(&err);
    if (!ok && errorOut) *errorOut = err;
    // Flag clears regardless of platform success — a clear that fails because
    // there is no entry is still "no key configured".
    if (auto *app = QCoreApplication::instance()) {
        for (QObject *child : app->children()) {
            if (auto *s = qobject_cast<ApplicationSettings *>(child)) {
                s->setCommitMessageApiKeyConfigured(false);
                emit apiKeyConfiguredChanged(false);
                return ok;
            }
        }
    }
    ApplicationSettings transient;
    transient.setCommitMessageApiKeyConfigured(false);
    emit apiKeyConfiguredChanged(false);
    return ok;
}

bool CredentialStore::isBackendAvailable() const
{
    return platformAvailable();
}

} // namespace ai
