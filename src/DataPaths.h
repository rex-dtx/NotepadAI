#pragma once

#include <cstdint>
#include <QString>

class DataPaths
{
public:
    enum class Source : std::uint8_t {
        Default,
        CLI,
        Env,
        Portable,
        Setting
    };

    static void init(const QString &resolvedBaseDir, Source source);

    static const QString &appDataLocation();

    static const QString &baseDir();

    static bool isCustom();

    static Source source();

    static QString sourceLabel();

private:
    DataPaths() = delete;
};
