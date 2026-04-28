/*
 * Copyright 2013-2022 MultiMC Contributors
 * Copyright 2022 kb1000
 *
 * This source is subject to the Microsoft Permissive License (MS-PL).
 * Please see the COPYING.md file for more information.
 */

#include "CurseForgeModel.h"
#include "Application.h"
#include "Json.h"

#include <QIcon>

CurseForge::ListModel::ListModel(QObject *parent) : QAbstractListModel(parent)
{
}

CurseForge::ListModel::~ListModel() = default;

QVariant CurseForge::ListModel::data(const QModelIndex &index, int role) const
{
    int pos = index.row();
    if (pos >= modpacks.size() || pos < 0 || !index.isValid())
    {
        return QString("INVALID INDEX %1").arg(pos);
    }

    auto pack = modpacks.at(pos);
    if (role == Qt::DisplayRole)
    {
        return pack.name;
    }
    else if (role == Qt::DecorationRole)
    {
        QString packIdStr = QString::number(pack.id);
        if (m_logoMap.contains(packIdStr))
        {
            return (m_logoMap.value(packIdStr));
        }
        QIcon icon = APPLICATION->getThemedIcon("screenshot-placeholder");
        ((ListModel *)this)->requestLogo(packIdStr, pack.iconUrl);
        return icon;
    }
    else if (role == Qt::ToolTipRole)
    {
        return pack.description;
    }
    else if (role == Qt::UserRole)
    {
        QVariant v;
        v.setValue(pack);
        return v;
    }
    return QVariant();
}

bool CurseForge::ListModel::canFetchMore(const QModelIndex &parent) const
{
    return searchState == CanPossiblyFetchMore;
}

void CurseForge::ListModel::fetchMore(const QModelIndex &parent)
{
    if (parent.isValid())
        return;
    if (nextSearchOffset == 0) {
        qWarning() << "fetchMore with 0 offset is wrong...";
        return;
    }
    performPaginatedSearch();
}

int CurseForge::ListModel::columnCount(const QModelIndex &parent) const
{
    return 1;
}

int CurseForge::ListModel::rowCount(const QModelIndex &parent) const
{
    return modpacks.size();
}

void CurseForge::ListModel::searchWithTerm(const QString &term, int sortField)
{
    if (currentSearchTerm == term && currentSearchTerm.isNull() == term.isNull() && currentSortField == sortField) {
        return;
    }
    currentSearchTerm = term;
    currentSortField = sortField;
    if (jobPtr) {
        jobPtr->abort();
        searchState = ResetRequested;
        return;
    }
    else {
        beginResetModel();
        modpacks.clear();
        endResetModel();
        searchState = None;
    }
    nextSearchOffset = 0;
    performPaginatedSearch();
}

void CurseForge::ListModel::performPaginatedSearch()
{
    QString apiKey = CurseForge::getApiKey();
    if (apiKey.isEmpty()) {
        qWarning() << "CurseForge API key is not set. Cannot perform search.";
        searchState = Finished;
        return;
    }

    auto *netJob = new NetJob("CurseForge::Search", APPLICATION->network());
    QUrl searchUrl = CurseForge::buildSearchUrl(currentSearchTerm, CurseForge::CLASS_ID_MODPACKS,
                                                 currentSortField, "desc", 25, nextSearchOffset);
    searchUrl = CurseForge::addApiKey(searchUrl, apiKey);

    netJob->addNetAction(Net::Download::makeByteArray(searchUrl, &response));
    jobPtr = netJob;
    jobPtr->start();
    QObject::connect(netJob, &NetJob::succeeded, this, &ListModel::searchRequestFinished);
    QObject::connect(netJob, &NetJob::failed, this, &ListModel::searchRequestFailed);
}

void CurseForge::ListModel::searchRequestFinished()
{
    jobPtr.reset();

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(response, &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        qWarning() << "Error while parsing JSON response from CurseForge at " << parse_error.offset << " reason: " << parse_error.errorString();
        qWarning() << response;
        return;
    }

    QVector<CurseForge::Modpack> newList;
    QJsonArray data;
    int total = 0;

    try
    {
        auto obj = Json::requireObject(doc);
        data = Json::requireArray(obj, "data");
        auto pagination = Json::requireObject(obj, "pagination");
        total = Json::requireInteger(pagination, "totalCount");
    }
    catch (const JSONValidationError &e)
    {
        qWarning() << "Error while parsing response from CurseForge: " << e.cause();
        return;
    }

    for (auto packRaw : data)
    {
        auto packObj = packRaw.toObject();
        CurseForge::Modpack pack;
        try
        {
            pack.id = Json::requireInteger(packObj, "id");
            pack.name = Json::requireString(packObj, "name");
            pack.description = Json::ensureString(packObj, "summary", "");
            pack.downloadCount = Json::ensureInteger(packObj, "downloadCount", 0);

            // Parse icon URL from nested logo object
            auto logoObj = Json::ensureObject(packObj, "logo", {});
            if (!logoObj.isEmpty()) {
                pack.iconUrl = Json::ensureUrl(logoObj, "thumbnailUrl", QUrl());
            }

            // Parse first author from authors array
            auto authorsArray = Json::ensureArray(packObj, "authors", {});
            if (!authorsArray.isEmpty()) {
                auto firstAuthor = authorsArray[0].toObject();
                pack.author = Json::ensureString(firstAuthor, "name", "");
            }

            newList.append(pack);
        }
        catch (const JSONValidationError &e)
        {
            qWarning() << "Error while loading pack from CurseForge: " << e.cause();
            continue;
        }
    }

    // Determine if there are more results to fetch
    if ((total - nextSearchOffset) <= 25)
        searchState = Finished;
    else
    {
        nextSearchOffset += 25;
        searchState = CanPossiblyFetchMore;
    }
    beginInsertRows(QModelIndex(), modpacks.size(), modpacks.size() + newList.size() - 1);
    for (auto item : newList) {
        modpacks.append(item);
    }
    endInsertRows();
}

void CurseForge::ListModel::searchRequestFailed()
{
    jobPtr.reset();

    if (searchState == ResetRequested)
    {
        beginResetModel();
        modpacks.clear();
        endResetModel();

        nextSearchOffset = 0;
        performPaginatedSearch();
    }
    else
    {
        searchState = Finished;
    }
}

void CurseForge::ListModel::logoLoaded(const QString &logo, const QIcon &out)
{
    m_loadingLogos.removeAll(logo);
    m_logoMap.insert(logo, out);
    for (int i = 0; i < modpacks.size(); i++) {
        if (QString::number(modpacks[i].id) == logo) {
            emit dataChanged(createIndex(i, 0), createIndex(i, 0), {Qt::DecorationRole});
        }
    }
}

void CurseForge::ListModel::logoFailed(const QString &logo)
{
    m_failedLogos.append(logo);
    m_loadingLogos.removeAll(logo);
}

void CurseForge::ListModel::requestLogo(const QString &logo, const QUrl &url)
{
    if (m_loadingLogos.contains(logo) || m_failedLogos.contains(logo))
    {
        return;
    }

    MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry("CurseForgePacks", QString("logos/%1").arg(logo.section(".", 0, 0)));
    auto *job = new NetJob(QString("CurseForge Icon Download %1").arg(logo), APPLICATION->network());
    job->addNetAction(Net::Download::makeCached(url, entry));

    auto fullPath = entry->getFullPath();
    QObject::connect(job, &NetJob::succeeded, this, [this, logo, fullPath]
    {
        QIcon icon(fullPath);
        QSize size = icon.actualSize(QSize(48, 48));
        if (size.width() < 48 && size.height() < 48)
        {
            icon = icon.pixmap(48, 48).scaled(48, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        }
        logoLoaded(logo, icon);
        if (waitingCallbacks.contains(logo))
        {
            waitingCallbacks.value(logo)(fullPath);
        }
    });

    QObject::connect(job, &NetJob::failed, this, [this, logo]
    {
        logoFailed(logo);
    });

    job->start();

    m_loadingLogos.append(logo);
}

void CurseForge::ListModel::getPackDetails(int id)
{
    auto index = getIndexFromId(id);
    if (!index) {
        return;
    }

    if (isPackDetailInProgress()) {
        queuedPackDetailRequest = id;
        cancelPackDetail();
        return;
    }

    currentPackDetailRequest = id;

    QString apiKey = CurseForge::getApiKey();
    if (apiKey.isEmpty()) {
        qWarning() << "CurseForge API key is not set. Cannot fetch pack details.";
        return;
    }

    auto &modpack = modpacks[*index];

    // Fetch details
    if (modpack.detailsLoaded != LoadState::Loaded)
    {
        QUrl detailsUrl = CurseForge::buildModDetailsUrl(id);
        detailsUrl = CurseForge::addApiKey(detailsUrl, apiKey);

        auto *netJob = new NetJob("CurseForge::PackDetails", APPLICATION->network());
        netJob->addNetAction(Net::Download::makeByteArray(detailsUrl, &detailsResponse));
        detailsPtr = netJob;
        detailsPtr->start();
        QObject::connect(netJob, &NetJob::succeeded, this, &ListModel::detailsRequestFinished);
        QObject::connect(netJob, &NetJob::failed, this, &ListModel::detailsRequestFailed);
    }

    // Fetch files (versions)
    if (modpack.versionsLoaded != LoadState::Loaded)
    {
        QUrl filesUrl = CurseForge::buildModFilesUrl(id);
        filesUrl = CurseForge::addApiKey(filesUrl, apiKey);

        auto *netJob = new NetJob("CurseForge::PackFiles", APPLICATION->network());
        netJob->addNetAction(Net::Download::makeByteArray(filesUrl, &versionsResponse));
        versionsPtr = netJob;
        versionsPtr->start();
        QObject::connect(netJob, &NetJob::succeeded, this, &ListModel::versionsRequestFinished);
        QObject::connect(netJob, &NetJob::failed, this, &ListModel::versionsRequestFailed);
    }
}

bool CurseForge::ListModel::isPackDetailInProgress()
{
    return detailsPtr || versionsPtr;
}

void CurseForge::ListModel::cancelPackDetail()
{
    if (detailsPtr) {
        detailsPtr->abort();
    }
    if (versionsPtr) {
        versionsPtr->abort();
    }
}

nonstd::optional<int> CurseForge::ListModel::getIndexFromId(int id)
{
    for (int i = 0; i < modpacks.size(); i++) {
        if (modpacks[i].id == id) {
            return i;
        }
    }
    return nonstd::nullopt;
}

nonstd::optional<CurseForge::Modpack> CurseForge::ListModel::getModpackById(int id)
{
    auto index = getIndexFromId(id);
    if (!index) {
        return nonstd::nullopt;
    }
    return modpacks[*index];
}

namespace {
bool parseDetailsInto(QByteArray &input, CurseForge::Modpack &output) {
    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(input, &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        qWarning() << "Error while parsing pack details response from CurseForge at " << parse_error.offset << " reason: " << parse_error.errorString();
        qWarning() << input;
        return false;
    }

    try
    {
        auto obj = Json::requireObject(doc);
        auto dataObj = Json::requireObject(obj, "data");
        output.body = Json::ensureString(dataObj, "body", "");
        return true;
    }
    catch (const JSONValidationError &e)
    {
        qWarning() << "Error while parsing response from CurseForge: " << e.cause();
        return false;
    }
}
}  // namespace

void CurseForge::ListModel::detailsRequestFinished()
{
    auto index = getIndexFromId(currentPackDetailRequest);
    if (index) {
        auto &modpack = modpacks[*index];

        if (parseDetailsInto(detailsResponse, modpack)) {
            modpack.detailsLoaded = LoadState::Loaded;
        }
        else {
            modpack.detailsLoaded = LoadState::Errored;
        }
        emit packDataChanged(currentPackDetailRequest);
    }
    detailsPtr.reset();
    checkDetailsDone();
}

void CurseForge::ListModel::detailsRequestFailed()
{
    auto index = getIndexFromId(currentPackDetailRequest);
    if (index) {
        auto &modpack = modpacks[*index];
        if (modpack.detailsLoaded == LoadState::NotLoaded) {
            modpack.detailsLoaded = LoadState::Errored;
            emit packDataChanged(currentPackDetailRequest);
        }
    }
    detailsPtr.reset();
    checkDetailsDone();
}

/*
 * CurseForge file response format:
 * {
 *   "data": [
 *     {
 *       "id": 67890,
 *       "displayName": "1.0.0",
 *       "fileName": "pack-1.0.0.zip",
 *       "fileDate": "2024-01-01T00:00:00Z",
 *       "fileLength": 12345678,
 *       "downloadUrl": "https://edge.forgecdn.net/files/...",
 *       "gameVersions": ["1.20.1"],
 *       "modLoaderTypes": [{"id": 1, "name": "Forge"}],
 *       "releaseType": 1,
 *       "downloadCount": 12345,
 *       "hashes": [{"value": "abc...", "algo": 1}]
 *     }
 *   ]
 * }
 *
 * releaseType values: 1 = Release, 2 = Beta, 3 = Alpha
 * hash algo values: 1 = MD5, 2 = SHA1
 */

namespace {

bool parseVersionsInto(QByteArray &input, CurseForge::Modpack &output) {
    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(input, &parse_error);
    if (parse_error.error != QJsonParseError::NoError)
    {
        qWarning() << "Error while parsing pack versions response from CurseForge at " << parse_error.offset << " reason: " << parse_error.errorString();
        qWarning() << input;
        return false;
    }

    try
    {
        auto obj = Json::requireObject(doc);
        auto dataArray = Json::requireArray(obj, "data");

        QVector<CurseForge::ModVersion> newList;
        for (auto item : dataArray)
        {
            auto fileObj = item.toObject();
            CurseForge::ModVersion version;
            try
            {
                version.fileId = Json::requireInteger(fileObj, "id");
                version.displayName = Json::ensureString(fileObj, "displayName", "");
                version.downloadCount = Json::ensureInteger(fileObj, "downloadCount", 0);
                version.download.size = Json::ensureInteger(fileObj, "fileLength", 0);
                version.download.filename = Json::ensureString(fileObj, "fileName", "");
                version.download.url = Json::ensureString(fileObj, "downloadUrl", "");

                // Parse release type: 1=Release, 2=Beta, 3=Alpha
                int releaseType = Json::ensureInteger(fileObj, "releaseType", 0);
                switch (releaseType) {
                    case 1:
                        version.type = CurseForge::VersionType::Release;
                        break;
                    case 2:
                        version.type = CurseForge::VersionType::Beta;
                        break;
                    case 3:
                        version.type = CurseForge::VersionType::Alpha;
                        break;
                    default:
                        version.type = CurseForge::VersionType::Unknown;
                        break;
                }

                // Parse date
                version.dateReleased = Json::ensureDateTime(fileObj, "fileDate", QDateTime());

                // Parse game versions - take the first Minecraft version
                auto gameVersions = Json::ensureArray(fileObj, "gameVersions", {});
                for (auto gv : gameVersions) {
                    QString gvStr = gv.toString();
                    // Skip version strings that look like loader names
                    if (gvStr == "Forge" || gvStr == "Fabric" || gvStr == "Quilt" || gvStr == "NeoForge")
                        continue;
                    version.gameVersion = gvStr;
                    break;
                }

                // Parse mod loader types
                auto loaderArray = Json::ensureArray(fileObj, "modLoaderTypes", {});
                if (!loaderArray.isEmpty()) {
                    auto firstLoader = loaderArray[0].toObject();
                    int loaderId = Json::ensureInteger(firstLoader, "id", 0);
                    switch (loaderId) {
                        case 1:
                            version.loaderType = CurseForge::ModLoaderType::Forge;
                            break;
                        case 4:
                            version.loaderType = CurseForge::ModLoaderType::Fabric;
                            break;
                        case 5:
                            version.loaderType = CurseForge::ModLoaderType::Quilt;
                            break;
                        case 6:
                            version.loaderType = CurseForge::ModLoaderType::NeoForge;
                            break;
                        default:
                            version.loaderType = CurseForge::ModLoaderType::Unknown;
                            break;
                    }
                }

                // Parse hashes - look for MD5 (algo 1)
                auto hashesArray = Json::ensureArray(fileObj, "hashes", {});
                for (auto h : hashesArray) {
                    auto hashObj = h.toObject();
                    int algo = Json::ensureInteger(hashObj, "algo", 0);
                    if (algo == 1) {  // MD5
                        version.download.md5 = Json::ensureString(hashObj, "value", "");
                    }
                }

                // Check if the file is a valid modpack zip
                if (version.download.filename.endsWith(".zip")) {
                    version.download.valid = true;
                }

                if (version.download.valid) {
                    newList.append(version);
                }
            }
            catch (const JSONValidationError &e)
            {
                qWarning() << "Error while loading version from CurseForge: " << e.cause();
                continue;
            }
        }
        output.versions = newList;
        return true;
    }
    catch (const JSONValidationError &e)
    {
        qWarning() << "Error while parsing response from CurseForge: " << e.cause();
        return false;
    }
}
}  // namespace

void CurseForge::ListModel::versionsRequestFinished()
{
    auto index = getIndexFromId(currentPackDetailRequest);
    if (index) {
        auto &modpack = modpacks[*index];
        parseVersionsInto(versionsResponse, modpack);
        modpack.versionsLoaded = LoadState::Loaded;
        emit packDataChanged(currentPackDetailRequest);
    }
    versionsPtr.reset();
    checkDetailsDone();
}

void CurseForge::ListModel::versionsRequestFailed()
{
    auto index = getIndexFromId(currentPackDetailRequest);
    if (index) {
        auto &modpack = modpacks[*index];
        if (modpack.versionsLoaded == LoadState::NotLoaded) {
            modpack.versionsLoaded = LoadState::Errored;
            emit packDataChanged(currentPackDetailRequest);
        }
    }
    versionsPtr.reset();
    checkDetailsDone();
}

void CurseForge::ListModel::checkDetailsDone()
{
    if (isPackDetailInProgress()) {
        return;
    }

    // all detail requests are finished
    currentPackDetailRequest = 0;

    // is there a new one queued?
    if (queuedPackDetailRequest != 0) {
        getPackDetails(queuedPackDetailRequest);
        queuedPackDetailRequest = 0;
    }
}
