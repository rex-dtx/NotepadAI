#include "PandocExporter.h"

#include <QDir>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTimer>

static QString s_pandocPath;
static int s_versionMajor = -1;
static int s_versionMinor = -1;
static bool s_versionChecked = false;

static QStringList pandocFallbackLocations()
{
    QStringList candidates;
#if defined(Q_OS_WIN)
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    if (!localAppData.isEmpty())
        candidates << localAppData + QStringLiteral("/Pandoc/pandoc.exe");
    const QString programFiles = qEnvironmentVariable("ProgramFiles");
    if (!programFiles.isEmpty())
        candidates << programFiles + QStringLiteral("/Pandoc/pandoc.exe");
    const QString programFilesX86 = qEnvironmentVariable("ProgramFiles(x86)");
    if (!programFilesX86.isEmpty())
        candidates << programFilesX86 + QStringLiteral("/Pandoc/pandoc.exe");
#elif defined(Q_OS_MAC)
    candidates << QStringLiteral("/opt/homebrew/bin/pandoc")
               << QStringLiteral("/usr/local/bin/pandoc")
               << QStringLiteral("/usr/bin/pandoc");
#else
    candidates << QStringLiteral("/usr/bin/pandoc")
               << QStringLiteral("/usr/local/bin/pandoc")
               << QDir::homePath() + QStringLiteral("/.local/bin/pandoc");
#endif
    return candidates;
}

PandocExporter::PandocExporter(QWidget *parentWidget, QObject *parent)
    : QObject(parent)
    , m_parentWidget(parentWidget)
{
}

QString PandocExporter::pandocExecutable()
{
    QString path = QStandardPaths::findExecutable(QStringLiteral("pandoc"));
    if (!path.isEmpty())
        return path;

    for (const QString &candidate : pandocFallbackLocations()) {
        if (QFileInfo(candidate).isExecutable())
            return candidate;
    }
    return QString();
}

bool PandocExporter::checkVersion()
{
    const QString exe = pandocExecutable();
    if (exe.isEmpty())
        return false;

    if (s_versionChecked && s_pandocPath == exe)
        return s_versionMajor > 2 || (s_versionMajor == 2 && s_versionMinor >= 19);

    s_pandocPath = exe;
    s_versionChecked = true;
    s_versionMajor = -1;
    s_versionMinor = -1;

    QProcess proc;
    proc.setProgram(exe);
    proc.setArguments({QStringLiteral("--version")});
    proc.start(QIODevice::ReadOnly);
    if (!proc.waitForFinished(5000))
        return false;

    const QString output = QString::fromUtf8(proc.readAllStandardOutput());
    static const QRegularExpression re(QStringLiteral(R"(pandoc\s+(\d+)\.(\d+))"));
    const QRegularExpressionMatch match = re.match(output);
    if (!match.hasMatch())
        return false;

    s_versionMajor = match.captured(1).toInt();
    s_versionMinor = match.captured(2).toInt();
    return s_versionMajor > 2 || (s_versionMajor == 2 && s_versionMinor >= 19);
}

bool PandocExporter::isAvailable()
{
    return checkVersion();
}

QStringList PandocExporter::buildArguments(const QString &inputPath,
                                           const QString &outputPath,
                                           Format format) const
{
    QStringList args;
    args << inputPath;
    if (format == Html)
        args << QStringLiteral("--embed-resources") << QStringLiteral("--standalone");
    args << QStringLiteral("-o") << outputPath;
    return args;
}

void PandocExporter::exportFile(const QString &inputMdPath,
                                const QString &outputPath,
                                Format format)
{
    const QString exe = pandocExecutable();
    if (exe.isEmpty()) {
        emit finished(false, tr("pandoc not found on PATH"));
        deleteLater();
        return;
    }

    m_proc = new QProcess(this);
    m_proc->setProgram(exe);
    m_proc->setArguments(buildArguments(inputMdPath, outputPath, format));
    m_proc->setWorkingDirectory(QFileInfo(inputMdPath).absolutePath());

    connect(m_proc, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, &PandocExporter::onProcessFinished);
    connect(m_proc, &QProcess::errorOccurred,
            this, &PandocExporter::onProcessError);

    m_timeout = new QTimer(this);
    m_timeout->setSingleShot(true);
    m_timeout->setInterval(30000);
    connect(m_timeout, &QTimer::timeout, this, &PandocExporter::onTimeout);

    m_proc->start(QIODevice::ReadOnly);
    m_timeout->start();
}

void PandocExporter::onProcessFinished(int exitCode, QProcess::ExitStatus status)
{
    m_timeout->stop();
    if (status == QProcess::CrashExit || exitCode != 0) {
        const QString err = QString::fromUtf8(m_proc->readAllStandardError()).trimmed();
        emit finished(false, err.isEmpty() ? tr("pandoc exited with code %1").arg(exitCode) : err);
    } else {
        emit finished(true, QString());
    }
    deleteLater();
}

void PandocExporter::onProcessError(QProcess::ProcessError error)
{
    if (error == QProcess::FailedToStart) {
        m_timeout->stop();
        emit finished(false, tr("Failed to start pandoc: %1").arg(m_proc->errorString()));
        deleteLater();
    }
}

void PandocExporter::onTimeout()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->kill();
        m_proc->waitForFinished(1000);
    }
    emit finished(false, tr("pandoc timed out after 30 seconds"));
    deleteLater();
}
