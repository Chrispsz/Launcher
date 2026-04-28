/* Copyright 2013-2024 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "InstanceImportTask.h"
#include "BaseInstance.h"
#include "FileSystem.h"
#include "Application.h"
#include "MMCZip.h"
#include "NullInstance.h"
#include "settings/INISettingsObject.h"
#include "icons/IconUtils.h"
#include <QtConcurrentRun>

// FIXME: this does not belong here, it's Minecraft/Flame specific
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "Json.h"
#include <quazipdir.h>
#include "modplatform/modrinth/ModrinthPackManifest.h"
#include "modplatform/curseforge/CurseForgePackManifest.h"
#include "modplatform/curseforge/CurseForgeAPI.h"

#include "icons/IconList.h"
#include "Application.h"
#include "net/ChecksumValidator.h"

#include <algorithm>
#include <iterator>

InstanceImportTask::InstanceImportTask(const QUrl sourceUrl)
{
    m_sourceUrl = sourceUrl;
}

void InstanceImportTask::executeTask()
{
    if (m_sourceUrl.isLocalFile())
    {
        m_archivePath = m_sourceUrl.toLocalFile();
        processZipPack();
    }
    else
    {
        setStatus(tr("Baixando modpack:\n%1").arg(m_sourceUrl.toString()));
        m_downloadRequired = true;

        const QString path = m_sourceUrl.host() + '/' + m_sourceUrl.path();
        auto entry = APPLICATION->metacache()->resolveEntry("general", path);
        entry->setStale(true);
        m_filesNetJob = new NetJob(tr("Download do modpack"), APPLICATION->network());
        m_filesNetJob->addNetAction(Net::Download::makeCached(m_sourceUrl, entry));
        m_archivePath = entry->getFullPath();
        auto job = m_filesNetJob.get();
        connect(job, &NetJob::succeeded, this, &InstanceImportTask::downloadSucceeded);
        connect(job, &NetJob::progress, this, &InstanceImportTask::downloadProgressChanged);
        connect(job, &NetJob::failed, this, &InstanceImportTask::downloadFailed);
        m_filesNetJob->start();
    }
}

void InstanceImportTask::downloadSucceeded()
{
    processZipPack();
    m_filesNetJob.reset();
}

void InstanceImportTask::downloadFailed(QString reason)
{
    emitFailed(reason);
    m_filesNetJob.reset();
}

void InstanceImportTask::downloadProgressChanged(qint64 current, qint64 total)
{
    setProgress(current / 2, total);
}

void InstanceImportTask::processZipPack()
{
    setStatus(tr("Extraindo modpack"));
    QDir extractDir(m_stagingPath);
    qDebug() << "Attempting to create instance from" << m_archivePath;

    // open the zip and find relevant files in it
    m_packZip.reset(new QuaZip(m_archivePath));
    if (!m_packZip->open(QuaZip::mdUnzip))
    {
        emitFailed(tr("Não foi possível abrir o arquivo zip do modpack fornecido."));
        return;
    }

    QStringList blacklist = {"instance.cfg", "manifest.json"};
    QString mmcFound = MMCZip::findFolderOfFileInZip(m_packZip.get(), "instance.cfg");
    // Technic support removed
    QString modrinthFound = MMCZip::findFolderOfFileInZip(m_packZip.get(), "modrinth.index.json");
    QString curseForgeFound = MMCZip::findFolderOfFileInZip(m_packZip.get(), "manifest.json");
    QString root;
    if(!mmcFound.isNull())
    {
        // process as MultiMC instance/pack
        qDebug() << "MultiMC:" << mmcFound;
        root = mmcFound;
        m_modpackType = ModpackType::MultiMC;
    }
    else if(!modrinthFound.isNull())
    {
        // process as Modrinth pack
        qDebug() << "Modrinth:" << modrinthFound;
        root = modrinthFound;
        m_modpackType = ModpackType::Modrinth;
    }
    else if(!curseForgeFound.isNull())
    {
        // process as CurseForge pack (manifest.json without modrinth.index.json)
        qDebug() << "CurseForge:" << curseForgeFound;
        root = curseForgeFound;
        m_modpackType = ModpackType::CurseForge;
    }
    if(m_modpackType == ModpackType::Unknown)
    {
        emitFailed(tr("O arquivo não contém um tipo de modpack reconhecido."));
        return;
    }

    // make sure we extract just the pack
    m_extractFuture = QtConcurrent::run(QThreadPool::globalInstance(), MMCZip::extractSubDir, m_packZip.get(), root, extractDir.absolutePath());
    connect(&m_extractFutureWatcher, &QFutureWatcher<QStringList>::finished, this, &InstanceImportTask::extractFinished);
    connect(&m_extractFutureWatcher, &QFutureWatcher<QStringList>::canceled, this, &InstanceImportTask::extractAborted);
    m_extractFutureWatcher.setFuture(m_extractFuture);
}

void InstanceImportTask::extractFinished()
{
    m_packZip.reset();
    if (!m_extractFuture.result())
    {
        emitFailed(tr("Falha ao extrair modpack"));
        return;
    }
    QDir extractDir(m_stagingPath);

    qDebug() << "Fixing permissions for extracted pack files...";
    QDirIterator it(extractDir, QDirIterator::Subdirectories);
    while (it.hasNext())
    {
        auto filepath = it.next();
        QFileInfo file(filepath);
        auto permissions = QFile::permissions(filepath);
        auto origPermissions = permissions;
        if(file.isDir())
        {
            // Folder +rwx for current user
            permissions |= QFileDevice::Permission::ReadUser | QFileDevice::Permission::WriteUser | QFileDevice::Permission::ExeUser;
        }
        else
        {
            // File +rw for current user
            permissions |= QFileDevice::Permission::ReadUser | QFileDevice::Permission::WriteUser;
        }
        if(origPermissions != permissions)
        {
            if(!QFile::setPermissions(filepath, permissions))
            {
                logWarning(tr("Não foi possível corrigir as permissões de %1").arg(filepath));
            }
            else
            {
                qDebug() << "Fixed" << filepath;
            }
        }
    }

    switch(m_modpackType)
    {
        case ModpackType::MultiMC:
            processMultiMC();
            return;
        case ModpackType::Modrinth:
            processModrinth();
            return;
        case ModpackType::CurseForge:
            processCurseForge();
            return;
        case ModpackType::Unknown:
            emitFailed(tr("O arquivo não contém um tipo de modpack reconhecido."));
            return;
    }
}

void InstanceImportTask::extractAborted()
{
    emitFailed(tr("A importação da instância foi abortada."));
    return;
}

void InstanceImportTask::processMultiMC()
{
    QString configPath = FS::PathCombine(m_stagingPath, "instance.cfg");
    auto instanceSettings = std::make_shared<INISettingsObject>(configPath);
    instanceSettings->registerSetting("InstanceType", "Legacy");

    NullInstance instance(m_globalSettings, instanceSettings, m_stagingPath);

    // reset time played on import... because packs.
    instance.resetTimePlayed();

    // set a new nice name
    instance.setName(m_instName);

    // if the icon was specified by user, use that. otherwise pull icon from the pack
    if (m_instIcon != "default")
    {
        instance.setIconKey(m_instIcon);
    }
    else
    {
        m_instIcon = instance.iconKey();

        auto importIconPath = IconUtils::findBestIconIn(instance.instanceRoot(), m_instIcon);
        if (!importIconPath.isNull() && QFile::exists(importIconPath))
        {
            // import icon
            auto iconList = APPLICATION->icons();
            if (iconList->iconFileExists(m_instIcon))
            {
                iconList->deleteIcon(m_instIcon);
            }
            iconList->installIcons({importIconPath});
        }
    }
    emitSucceeded();
}

namespace {
bool mergeOverrides(const QString &fromDir, const QString &toDir) {
    QDir dir(fromDir);
    if(!dir.exists()) {
        return true;
    }
    if(!FS::ensureFolderPathExists(toDir)) {
        return false;
    }
    const int absSourcePathLength = dir.absoluteFilePath(fromDir).length();

    QDirIterator it(fromDir, QDirIterator::Subdirectories);
    while (it.hasNext()){
        it.next();
        const auto fileInfo = it.fileInfo();
        auto fileName = fileInfo.fileName();
        if(fileName == "." || fileName == "..") {
            continue;
        }
        const QString subPathStructure = fileInfo.absoluteFilePath().mid(absSourcePathLength);
        const QString constructedAbsolutePath = toDir + subPathStructure;

        if(fileInfo.isDir()){
            //Create directory in target folder
            dir.mkpath(constructedAbsolutePath);
        } else if(fileInfo.isFile()) {
            QFileInfo targetFileInfo(constructedAbsolutePath);
            if(targetFileInfo.exists()) {
                continue;
            }
            // move
            QFile::rename(fileInfo.absoluteFilePath(), constructedAbsolutePath);
        }
    }

    dir.removeRecursively();
    return true;
}

}

void InstanceImportTask::processModrinth() {
    std::vector<Modrinth::File> files;
    QString minecraftVersion, fabricVersion, forgeVersion, neoforgeVersion;
    try
    {
        QString indexPath = FS::PathCombine(m_stagingPath, "modrinth.index.json");
        auto doc = Json::requireDocument(indexPath);
        auto obj = Json::requireObject(doc, "modrinth.index.json");
        int formatVersion = Json::requireInteger(obj, "formatVersion", "modrinth.index.json");
        if (formatVersion == 1)
        {
            auto game = Json::requireString(obj, "game", "modrinth.index.json");
            if (game != "minecraft")
            {
                throw JSONValidationError("Unknown game: " + game);
            }

            auto jsonFiles = Json::requireIsArrayOf<QJsonObject>(obj, "files", "modrinth.index.json");
            for(auto & obj: jsonFiles) {
                Modrinth::File file;
                auto dirtyPath = Json::requireString(obj, "path");
                dirtyPath.replace('\\', '/');
                auto simplifiedPath = QDir::cleanPath(dirtyPath);
                QFileInfo fileInfo (simplifiedPath);
                if(simplifiedPath.startsWith("../") || simplifiedPath.contains("/../") || fileInfo.isAbsolute()) {
                    throw JSONValidationError("Invalid path found in modpack files:\n\n" + simplifiedPath);
                }
                file.path = simplifiedPath;

                // env doesn't have to be present, in that case mod is required
                auto env = Json::ensureObject(obj, "env");
                auto clientEnv = Json::ensureString(env, "client", "required");

                if(clientEnv == "required") {
                    // NOOP
                }
                else if(clientEnv == "optional") {
                    file.path += ".disabled";
                }
                else if(clientEnv == "unsupported") {
                    continue;
                }

                QJsonObject hashes = Json::requireObject(obj, "hashes");
                QString hash;
                QCryptographicHash::Algorithm hashAlgorithm;
                hash = Json::ensureString(hashes, "sha256");
                hashAlgorithm = QCryptographicHash::Sha256;
                if (hash.isEmpty())
                {
                    hash = Json::ensureString(hashes, "sha512");
                    hashAlgorithm = QCryptographicHash::Sha512;
                    if (hash.isEmpty())
                    {
                        hash = Json::ensureString(hashes, "sha1");
                        hashAlgorithm = QCryptographicHash::Sha1;
                        if (hash.isEmpty())
                        {
                            throw JSONValidationError("No hash found for: " + file.path);
                        }
                    }
                }
                file.hash = QByteArray::fromHex(hash.toLatin1());
                file.hashAlgorithm = hashAlgorithm;
                // Do not use requireUrl, which uses StrictMode, instead use QUrl's default TolerantMode (as Modrinth seems to incorrectly handle spaces)
                file.download = Json::requireValueString(Json::ensureArray(obj, "downloads").first(), "Download URL for " + file.path);
                if (!file.download.isValid())
                {
                    throw JSONValidationError("Download URL for " + file.path + " is not a correctly formatted URL");
                }
                files.push_back(file);
            }

            auto dependencies = Json::requireObject(obj, "dependencies", "modrinth.index.json");
            for (auto it = dependencies.begin(), end = dependencies.end(); it != end; ++it)
            {
                QString name = it.key();
                if (name == "minecraft")
                {
                    if (!minecraftVersion.isEmpty())
                        throw JSONValidationError("Duplicate Minecraft version");
                    minecraftVersion = Json::requireValueString(*it, "Minecraft version");
                }
                else if (name == "fabric-loader")
                {
                    if (!fabricVersion.isEmpty())
                        throw JSONValidationError("Duplicate Fabric Loader version");
                    fabricVersion = Json::requireValueString(*it, "Fabric Loader version");
                }
                else if (name == "forge")
                {
                    if (!forgeVersion.isEmpty())
                        throw JSONValidationError("Duplicate Forge version");
                    forgeVersion = Json::requireValueString(*it, "Forge version");
                }
                else if (name == "neoforge")
                {
                    if (!neoforgeVersion.isEmpty())
                        throw JSONValidationError("Duplicate NeoForge version");
                    neoforgeVersion = Json::requireValueString(*it, "NeoForge version");
                }
                else
                {
                    throw JSONValidationError("Unknown dependency type: " + name);
                }
            }
        }
        else
        {
            throw JSONValidationError(QStringLiteral("Unknown format version: %s").arg(formatVersion));
        }
        QFile::remove(indexPath);
    }
    catch (const JSONValidationError &e)
    {
        emitFailed(tr("Não foi possível entender o índice do pacote:\n") + e.cause());
        return;
    }
    QString clientOverridePath = FS::PathCombine(m_stagingPath, "client-overrides");
    if (QFile::exists(clientOverridePath)) {
        QString mcPath = FS::PathCombine(m_stagingPath, ".minecraft");
        if (!QFile::rename(clientOverridePath, mcPath)) {
            emitFailed(tr("Não foi possível renomear a pasta de overrides:\n") + "overrides");
            return;
        }
    }

    // TODO: only extract things we actually want instead of everything only to just delete it afterwards ...
    if(!mergeOverrides(FS::PathCombine(m_stagingPath, "client-overrides"), FS::PathCombine(m_stagingPath, ".minecraft"))) {
        emitFailed(tr("Não foi possível mesclar a pasta de overrides:\n") + "client-overrides");
        return;
    }

    if(!mergeOverrides(FS::PathCombine(m_stagingPath, "overrides"), FS::PathCombine(m_stagingPath, ".minecraft"))) {
        emitFailed(tr("Não foi possível mesclar a pasta de overrides:\n") + "overrides");
        return;
    }

    FS::deletePath(FS::PathCombine(m_stagingPath, "server-overrides"));


    QString configPath = FS::PathCombine(m_stagingPath, "instance.cfg");
    auto instanceSettings = std::make_shared<INISettingsObject>(configPath);
    instanceSettings->registerSetting("InstanceType", "Legacy");
    instanceSettings->set("InstanceType", "OneSix");
    MinecraftInstance instance(m_globalSettings, instanceSettings, m_stagingPath);
    auto components = instance.getPackProfile();
    components->buildingFromScratch();
    components->setComponentVersion("net.minecraft", minecraftVersion, true);
    if (!fabricVersion.isEmpty())
        components->setComponentVersion("net.fabricmc.fabric-loader", fabricVersion, true);
    if (!forgeVersion.isEmpty())
        components->setComponentVersion("net.minecraftforge", forgeVersion, true);
    if (!neoforgeVersion.isEmpty())
        components->setComponentVersion("net.neoforged", neoforgeVersion, true);
    if (m_instIcon != "default")
    {
        instance.setIconKey(m_instIcon);
    }
    instance.setName(m_instName);
    instance.saveNow();

    m_filesNetJob = new NetJob(tr("Download de mods"), APPLICATION->network());
    for (auto &file : files)
    {
        auto path = FS::PathCombine(m_stagingPath, ".minecraft", file.path);
        qDebug() << "Will download" << file.download << "to" << path;
        auto dl = Net::Download::makeFile(file.download, path);
        dl->addValidator(new Net::ChecksumValidator(file.hashAlgorithm, file.hash));
        m_filesNetJob->addNetAction(dl);
    }
    connect(m_filesNetJob.get(), &NetJob::succeeded, this, [&]()
    {
        m_filesNetJob.reset();
        emitSucceeded();
    });
    connect(m_filesNetJob.get(), &NetJob::failed, [&](const QString &reason)
    {
        m_filesNetJob.reset();
        emitFailed(reason);
    });
    connect(m_filesNetJob.get(), &NetJob::progress, [&](qint64 current, qint64 total)
    {
        setProgress(current, total);
    });
    setStatus(tr("Baixando mods..."));
    m_filesNetJob->start();
}

void InstanceImportTask::processCurseForge() {
    QString minecraftVersion;
    QString forgeVersion, fabricVersion, neoforgeVersion;
    std::vector<CurseForge::File> files;
    QString overridesDir;

    try {
        QString manifestPath = FS::PathCombine(m_stagingPath, "manifest.json");
        auto doc = Json::requireDocument(manifestPath);
        auto obj = Json::requireObject(doc, "manifest.json");

        // CurseForge manifest format
        auto mcObj = Json::requireObject(obj, "minecraft");
        minecraftVersion = Json::requireString(mcObj, "version");

        // Parse modloaders - extract version from id (format: "forge-47.2.0", "neoforge-47.1.65", etc.)
        auto modLoadersArray = Json::ensureArray(mcObj, "modLoaders");
        for (const auto &loaderVal : modLoadersArray) {
            auto loaderObj = Json::requireObject(loaderVal);
            QString loaderId = Json::requireString(loaderObj, "id");

            if (loaderId.startsWith("forge-")) {
                forgeVersion = loaderId.mid(6);
            } else if (loaderId.startsWith("neoforge-")) {
                neoforgeVersion = loaderId.mid(9);
            } else if (loaderId.startsWith("fabric-")) {
                fabricVersion = loaderId.mid(7);
            } else if (loaderId == "forge") {
                // Old format without version
                forgeVersion = QString();
            }
        }

        overridesDir = Json::ensureString(obj, "overrides", "overrides");

        // Parse files list (used to track mod references)
        auto filesArray = Json::ensureArray(obj, "files");
        for (const auto &fileVal : filesArray) {
            auto fileObj = Json::requireObject(fileVal);
            CurseForge::File file;
            file.projectID = QString::number(Json::requireInteger(fileObj, "projectID"));
            file.fileID = QString::number(Json::requireInteger(fileObj, "fileID"));
            file.required = Json::ensureInteger(fileObj, "required", 1);
            files.push_back(file);
        }

        QFile::remove(manifestPath);
    } catch (const JSONValidationError &e) {
        emitFailed(tr("Não foi possível analisar manifest.json:\n") + e.cause());
        return;
    }

    // Merge overrides
    QString overridesPath = FS::PathCombine(m_stagingPath, overridesDir);
    if (!mergeOverrides(overridesPath, FS::PathCombine(m_stagingPath, ".minecraft"))) {
        emitFailed(tr("Falha ao mesclar a pasta de overrides."));
        return;
    }
    FS::deletePath(FS::PathCombine(m_stagingPath, "server-overrides"));

    // Remove modlist.html if present (not needed)
    FS::deletePath(FS::PathCombine(m_stagingPath, "modlist.html"));

    // Create instance
    QString configPath = FS::PathCombine(m_stagingPath, "instance.cfg");
    auto instanceSettings = std::make_shared<INISettingsObject>(configPath);
    instanceSettings->registerSetting("InstanceType", "Legacy");
    instanceSettings->set("InstanceType", "OneSix");
    MinecraftInstance instance(m_globalSettings, instanceSettings, m_stagingPath);
    auto components = instance.getPackProfile();
    components->buildingFromScratch();
    components->setComponentVersion("net.minecraft", minecraftVersion, true);

    if (!forgeVersion.isEmpty())
        components->setComponentVersion("net.minecraftforge", forgeVersion, true);
    if (!fabricVersion.isEmpty())
        components->setComponentVersion("net.fabricmc.fabric-loader", fabricVersion, true);
    if (!neoforgeVersion.isEmpty())
        components->setComponentVersion("net.neoforged", neoforgeVersion, true);

    if (m_instIcon != "default") {
        instance.setIconKey(m_instIcon);
    }
    instance.setName(m_instName);
    instance.saveNow();

    // Download mod files using CurseForge API if API key is available
    QString apiKey = CurseForge::getApiKey();
    if (!apiKey.isEmpty() && !files.empty()) {
        // Filter only required files
        QVector<CurseForge::File> requiredFiles;
        for (const auto& file : files) {
            if (file.required == 1) {
                requiredFiles.push_back(file);
            }
        }

        if (!requiredFiles.isEmpty()) {
            setStatus(tr("Obtendo informações de arquivos de mods do CurseForge (%1 arquivos)...").arg(requiredFiles.size()));

            // Step 1: Fetch file metadata from CurseForge API to get download URLs
            m_filesNetJob = new NetJob(tr("Obtenção de informações de mods CurseForge"), APPLICATION->network());

            struct CFFileResponse {
                CurseForge::File file;
                QByteArray* response;
            };
            auto* responses = new QVector<CFFileResponse>();

            for (const auto& file : requiredFiles) {
                QString fileInfoUrl = QString(
                    "https://api.curseforge.com/v1/mods/%1/files/%2"
                ).arg(file.projectID).arg(file.fileID);

                auto* buf = new QByteArray();
                auto dl = Net::Download::makeByteArray(QUrl(fileInfoUrl), buf);
                CurseForge::addApiKeyHeader(dl.get(), apiKey);
                m_filesNetJob->addNetAction(dl);
                responses->push_back({file, buf});
            }

            // After fetching all file info, download the actual mod files
            connect(m_filesNetJob.get(), &NetJob::succeeded, this, [this, responses, apiKey]() {
                // Step 2: Parse responses to get download URLs and download actual files
                m_filesNetJob.reset();

                auto* downloadJob = new NetJob(tr("Download de mods CurseForge"), APPLICATION->network());
                QString modDir = FS::PathCombine(m_stagingPath, ".minecraft", "mods");
                FS::ensureFolderPathExists(modDir);
                bool anyDownloads = false;

                for (const auto& resp : *responses) {
                    QJsonParseError parseError;
                    QJsonDocument doc = QJsonDocument::fromJson(*resp.response, &parseError);
                    if (parseError.error != QJsonParseError::NoError) {
                        qWarning() << "Failed to parse CurseForge file info for project" << resp.file.projectID << "file" << resp.file.fileID;
                        continue;
                    }

                    try {
                        auto dataObj = Json::requireObject(Json::requireObject(doc), "data");
                        QString downloadUrl = Json::ensureString(dataObj, "downloadUrl", "");
                        QString fileName = Json::ensureString(dataObj, "fileName", "");

                        if (downloadUrl.isEmpty()) {
                            qWarning() << "No download URL for CurseForge file" << resp.file.fileID;
                            continue;
                        }

                        QString targetPath = FS::PathCombine(modDir, fileName);
                        qDebug() << "Will download CurseForge mod:" << downloadUrl << "to" << targetPath;
                        auto dl = Net::Download::makeFile(QUrl(downloadUrl), targetPath);
                        downloadJob->addNetAction(dl);
                        anyDownloads = true;
                    } catch (const JSONValidationError& e) {
                        qWarning() << "Error parsing CurseForge file response:" << e.cause();
                        continue;
                    }
                }

                // Clean up response buffers
                for (auto& resp : *responses) {
                    delete resp.response;
                }
                delete responses;

                if (!anyDownloads) {
                    qDebug() << "No downloadable CurseForge mods found";
                    emitSucceeded();
                    return;
                }

                m_filesNetJob = downloadJob;
                setStatus(tr("Baixando mods do CurseForge..."));
                connect(downloadJob, &NetJob::succeeded, this, [this]() {
                    m_filesNetJob.reset();
                    emitSucceeded();
                });
                connect(downloadJob, &NetJob::failed, this, [this](const QString& reason) {
                    qWarning() << "CurseForge mod download failed:" << reason;
                    m_filesNetJob.reset();
                    // Don't fail entirely - instance is still usable without mods
                    emitSucceeded();
                });
                connect(downloadJob, &NetJob::progress, this, [this](qint64 current, qint64 total) {
                    setProgress(current, total);
                });
                downloadJob->start();
            });

            connect(m_filesNetJob.get(), &NetJob::failed, this, [this, responses](const QString& reason) {
                qWarning() << "Failed to fetch CurseForge file info:" << reason;
                m_filesNetJob.reset();
                // Clean up response buffers
                for (auto& resp : *responses) {
                    delete resp.response;
                }
                delete responses;
                // Don't fail entirely - instance is still usable without mods
                emitSucceeded();
            });

            m_filesNetJob->start();
            return;
        }
    } else if (files.empty()) {
        qDebug() << "CurseForge modpack has no mod file references";
    } else {
        qDebug() << "CurseForge API key not configured - skipping mod downloads."
                 << "Configure it in Settings → Minecraft → CurseForge to enable automatic mod downloads.";
    }

    emitSucceeded();
}
