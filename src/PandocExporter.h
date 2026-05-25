#ifndef PANDOCEXPORTER_H
#define PANDOCEXPORTER_H

#include <QObject>
#include <QString>
#include <QProcess>

#include <cstdint>

class QTimer;
class QWidget;

class PandocExporter : public QObject
{
    Q_OBJECT
public:
    enum Format : std::uint8_t { Docx, Html, Epub };

    explicit PandocExporter(QWidget *parentWidget, QObject *parent = nullptr);

    static QString pandocExecutable();
    static bool isAvailable();

    void exportFile(const QString &inputMdPath,
                    const QString &outputPath,
                    Format format);

signals:
    void finished(bool success, const QString &errorMessage);

private slots:
    void onProcessFinished(int exitCode, QProcess::ExitStatus status);
    void onProcessError(QProcess::ProcessError error);
    void onTimeout();

private:
    static bool checkVersion();
    QStringList buildArguments(const QString &inputPath,
                              const QString &outputPath,
                              Format format) const;

    QWidget *m_parentWidget = nullptr;
    QProcess *m_proc = nullptr;
    QTimer *m_timeout = nullptr;
};

#endif // PANDOCEXPORTER_H
