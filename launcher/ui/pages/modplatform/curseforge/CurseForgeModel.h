/*
 * Copyright 2013-2022 MultiMC Contributors
 * Copyright 2022 kb1000
 *
 * This source is subject to the Microsoft Permissive License (MS-PL).
 * Please see the COPYING.md file for more information.
 */

#pragma once

#include "CurseForgeData.h"
#include "modplatform/curseforge/CurseForgeAPI.h"
#include "net/NetJob.h"

#include <QAbstractListModel>
#include <QVector>
#include <nonstd/optional>

namespace CurseForge {

using LogoCallback = std::function<void(QString)>;

class ListModel : public QAbstractListModel {
    Q_OBJECT

public:
    explicit ListModel(QObject *parent);
    ~ListModel() override;

    QVariant data(const QModelIndex &index, int role) const override;
    int columnCount(const QModelIndex &parent) const override;
    int rowCount(const QModelIndex &parent) const override;
    bool canFetchMore(const QModelIndex &parent) const override;
    void fetchMore(const QModelIndex &parent) override;

    void searchWithTerm(const QString &term, int sortField);
    void getPackDetails(int id);
    nonstd::optional<Modpack> getModpackById(int id);

signals:
    void packDataChanged(int id);

private slots:
    void searchRequestFinished();
    void searchRequestFailed();

    void detailsRequestFinished();
    void detailsRequestFailed();

    void versionsRequestFinished();
    void versionsRequestFailed();

private:
    void performPaginatedSearch();

    void logoFailed(const QString &logo);
    void logoLoaded(const QString &logo, const QIcon &out);

    void requestLogo(const QString &logo, const QUrl &url);

    nonstd::optional<int> getIndexFromId(int id);

    bool isPackDetailInProgress();
    void cancelPackDetail();
    void checkDetailsDone();

    QVector<Modpack> modpacks;
    QStringList m_failedLogos;
    QStringList m_loadingLogos;
    QMap<QString, QIcon> m_logoMap;
    QMap<QString, LogoCallback> waitingCallbacks;

    QString currentSearchTerm;
    int currentSortField = 2;  // SortFieldId::Popularity
    int nextSearchOffset = 0;
    int totalCount = 0;
    enum SearchState {
        None,
        CanPossiblyFetchMore,
        ResetRequested,
        Finished
    } searchState = None;

    int queuedPackDetailRequest = 0;
    int currentPackDetailRequest = 0;
    NetJob::Ptr jobPtr;
    QByteArray response;

    NetJob::Ptr detailsPtr;
    QByteArray detailsResponse;

    NetJob::Ptr versionsPtr;
    QByteArray versionsResponse;
};

}  // namespace CurseForge
