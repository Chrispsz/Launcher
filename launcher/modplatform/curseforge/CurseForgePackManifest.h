#pragma once

#include <QByteArray>
#include <QCryptographicHash>
#include <QString>
#include <QUrl>
#include <QJsonObject>
#include <QJsonArray>

namespace CurseForge {
struct File {
    QString projectID;
    QString fileID;
    int required = 1;  // 1 = required, 2 = optional
    QString downloadUrl;
    QString fileName;
    QByteArray sha1;
    uint32_t fileSize = 0;
};

struct Modloader {
    QString id;       // "forge", "fabric", "neoforge", "quilt"
    QString primary;  // true/false - whether this is the primary modloader
};

struct Manifest {
    int mcVersion = 0;  // Minecraft version (1 = MC 1.x format)
    QString minecraftVersion;
    QString name;
    QString version;
    QString author;
    QString description;
    QVector<Modloader> modLoaders;
    QVector<File> files;
    QString overrides;
};
}
