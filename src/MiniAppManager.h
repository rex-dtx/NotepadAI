/*
 * This file is part of NotepadAI.
 * Copyright 2024 NotepadAI contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include "MiniAppDefinition.h"

#include <QIcon>
#include <QList>
#include <QObject>

class DockedEditor;
class MiniAppInstance;
class MiniAppRegistry;
class NotepadNextApplication;

namespace ads { class CDockWidget; }

class MiniAppManager : public QObject
{
    Q_OBJECT

public:
    explicit MiniAppManager(NotepadNextApplication *app,
                            MiniAppRegistry *registry,
                            DockedEditor *dockedEditor,
                            QObject *parent = nullptr);
    ~MiniAppManager() override;

    void launchApp(const MiniAppDefinition &def);
    void shutdown();

    int runningCount() const { return m_instances.size(); }

signals:
    void instanceCountChanged(int count);

private:
    void onInstanceStateChanged(MiniAppInstance *instance);
    void onInstanceFinished(MiniAppInstance *instance);
    void retintAllIcons();
    QIcon tintedGlobeIcon() const;

    NotepadNextApplication *m_app;
    MiniAppRegistry *m_registry;
    DockedEditor *m_dockedEditor;
    QList<MiniAppInstance *> m_instances;
    QString m_iconPath;
};
