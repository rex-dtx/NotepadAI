#include "DataPaths.h"

#include <QCoreApplication>
#include <QDir>

namespace {
QString g_baseDir;
QString g_appDataLocation;
DataPaths::Source g_source = DataPaths::Source::Default;
bool g_initialized = false;
}

void DataPaths::init(const QString &resolvedBaseDir, Source source)
{
    g_baseDir = resolvedBaseDir;
    g_source = source;
    g_appDataLocation = QDir::cleanPath(resolvedBaseDir + QStringLiteral("/NotepadAI"));
    g_initialized = true;
}

const QString &DataPaths::appDataLocation()
{
    Q_ASSERT(g_initialized);
    return g_appDataLocation;
}

const QString &DataPaths::baseDir()
{
    Q_ASSERT(g_initialized);
    return g_baseDir;
}

bool DataPaths::isCustom()
{
    return g_source != Source::Default;
}

DataPaths::Source DataPaths::source()
{
    return g_source;
}

QString DataPaths::sourceLabel()
{
    switch (g_source) {
    case Source::CLI:      return QCoreApplication::translate("DataPaths", "Set by --data-dir");
    case Source::Env:      return QCoreApplication::translate("DataPaths", "Set by NOTEPADAI_DATA_DIR");
    case Source::Portable: return QCoreApplication::translate("DataPaths", "Portable mode");
    case Source::Setting:  return QCoreApplication::translate("DataPaths", "Set in Preferences");
    case Source::Default:  return QCoreApplication::translate("DataPaths", "Default");
    }
    return {};
}
