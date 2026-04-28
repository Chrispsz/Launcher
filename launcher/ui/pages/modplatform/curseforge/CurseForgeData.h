/*
 * Copyright 2022 kb1000
 *
 * This source is subject to the Microsoft Permissive License (MS-PL).
 * Please see the COPYING.md file for more information.
 */

#pragma once

#include <QString>
#include <QMetaType>
#include <QUrl>
#include <QDateTime>
#include <QVector>

namespace CurseForge {

enum class LoadState {
    NotLoaded = 0,
    Loaded = 1,
    Errored = 2
};

enum class VersionType {
    Alpha,
    Beta,
    Release,
    Unknown
};

enum class ModLoaderType {
    Forge = 1,
    Fabric = 4,
    Quilt = 5,
    NeoForge = 6,
    Unknown = 0
};

struct Download {
    bool valid = false;
    QString filename;
    QString url;
    QString md5;  // CurseForge uses MD5
    uint64_t size = 0;
    bool primary = false;
};

struct ModVersion {
    int fileId = 0;
    QString displayName;
    Download download;
    QString gameVersion;  // e.g. "1.20.1"
    ModLoaderType loaderType = ModLoaderType::Unknown;
    QDateTime dateReleased;
    VersionType type = VersionType::Unknown;
    uint64_t downloadCount = 0;
};

struct Modpack {
    int id = 0;  // CurseForge project ID
    QString name;
    QUrl iconUrl;
    QString author;
    QString description;
    uint64_t downloadCount = 0;

    LoadState detailsLoaded = LoadState::NotLoaded;
    QString body;

    LoadState versionsLoaded = LoadState::NotLoaded;
    QVector<ModVersion> versions;
};

struct ModInfo {
    int id = 0;
    QString name;
    QString slug;
    QString author;
    QString description;
    QUrl iconUrl;
    uint64_t downloadCount = 0;
    QDateTime dateModified;

    bool operator==(const ModInfo& other) const { return id == other.id; }
};

}  // namespace CurseForge

Q_DECLARE_METATYPE(CurseForge::Download)
Q_DECLARE_METATYPE(CurseForge::ModVersion)
Q_DECLARE_METATYPE(CurseForge::Modpack)
Q_DECLARE_METATYPE(CurseForge::ModInfo)
