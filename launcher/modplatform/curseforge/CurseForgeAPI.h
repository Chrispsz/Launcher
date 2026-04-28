/*
 * Copyright 2022 kb1000
 *
 * This source is subject to the Microsoft Permissive License (MS-PL).
 * Please see the COPYING.md file for more information.
 */

#pragma once

#include <QString>
#include <QUrl>
#include "Application.h"

namespace CurseForge {

/*
 * CurseForge Core API v1 constants and helpers
 * Documentation: https://docs.curseforge.com/
 */

// API base URL
static const QString API_BASE = QStringLiteral("https://api.curseforge.com/v1");

// Default (shared) API key — used when no user-configured key is set.
// Users can override this in Settings → Minecraft → CurseForge.
static const QString DEFAULT_API_KEY = QStringLiteral("$2a$10$7sbUGEKY9ZLpEEEsjDrMHej9u6zK57n7qXpAkpTF4gcu0Y5fXmotm");

// Game ID for Minecraft
static const int MINECRAFT_GAME_ID = 432;

// Class IDs (content categories)
static const int CLASS_ID_MODS = 6;
static const int CLASS_ID_MODPACKS = 4471;
static const int CLASS_ID_RESOURCE_PACKS = 12;
static const int CLASS_ID_SHADER_PACKS = 6552;

// Mod loader type IDs
enum class ModLoaderTypeId {
    Forge = 1,
    Fabric = 4,
    Quilt = 5,
    NeoForge = 6
};

// Sort field IDs
enum class SortFieldId {
    Popularity = 2,
    LastUpdated = 3,
    TotalDownloads = 6,
    GameVersion = 8
};

/*
 * Retrieve the CurseForge API key.
 * Returns the user-configured key from settings if set,
 * otherwise falls back to the built-in default key.
 */
inline QString getApiKey()
{
    QString userKey = APPLICATION->settings()->get("CurseForgeAPIKey").toString();
    if (!userKey.isEmpty())
        return userKey;
    return DEFAULT_API_KEY;
}

/*
 * Append the API key as a query parameter to the given URL.
 * If the URL already has query params, appends with '&', otherwise with '?'.
 */
inline QUrl addApiKey(const QUrl &url, const QString &apiKey)
{
    if (apiKey.isEmpty())
        return url;

    QString urlString = url.toString();
    urlString += (urlString.contains("?") ? QStringLiteral("&") : QStringLiteral("?")) + QStringLiteral("apiKey=") + apiKey;
    return QUrl(urlString);
}

/*
 * Build a search URL for modpacks (or other class types).
 *
 * @param searchTerm  The text to search for (empty for popular listing)
 * @param classId     The class ID to filter by (default: modpacks)
 * @param sortField   The sort field ID (default: popularity)
 * @param sortOrder   Sort order, "asc" or "desc" (default: "desc")
 * @param pageSize    Number of results per page (default: 25)
 * @param indexOffset Pagination offset (default: 0)
 * @return QUrl       The fully constructed API URL (without API key)
 */
inline QUrl buildSearchUrl(const QString &searchTerm = {},
                           int classId = CLASS_ID_MODPACKS,
                           int sortField = static_cast<int>(SortFieldId::Popularity),
                           const QString &sortOrder = QStringLiteral("desc"),
                           int pageSize = 25,
                           int indexOffset = 0)
{
    QString url = QString("%1/mods/search?gameId=%2&classId=%3&sortField=%4&sortOrder=%5&pageSize=%6&indexOffset=%7")
                      .arg(API_BASE)
                      .arg(MINECRAFT_GAME_ID)
                      .arg(classId)
                      .arg(sortField)
                      .arg(sortOrder)
                      .arg(pageSize)
                      .arg(indexOffset);

    if (!searchTerm.isEmpty()) {
        url += "&searchFilter=" + searchTerm;
    }

    return QUrl(url);
}

/*
 * Build a mod details URL.
 * @param modId  The CurseForge project/mod ID
 * @return QUrl  The details API URL
 */
inline QUrl buildModDetailsUrl(int modId)
{
    return QUrl(QString("%1/mods/%2").arg(API_BASE).arg(modId));
}

/*
 * Build a mod files (versions) URL.
 * @param modId  The CurseForge project/mod ID
 * @return QUrl  The files API URL
 */
inline QUrl buildModFilesUrl(int modId)
{
    return QUrl(QString("%1/mods/%2/files").arg(API_BASE).arg(modId));
}

}  // namespace CurseForge
