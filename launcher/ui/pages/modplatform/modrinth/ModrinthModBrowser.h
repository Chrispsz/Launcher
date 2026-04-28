#pragma once

#include <QDialog>
#include <QAbstractListModel>
#include <QSortFilterProxyModel>
#include "net/NetJob.h"

class ModFolderModel;
class BaseInstance;

namespace Ui {
    class ModrinthModBrowser;
}

namespace ModrinthModBrowserNS {

struct ModInfo {
    QString id;
    QString name;
    QString author;
    QString description;
    QUrl iconUrl;
    QString downloadUrl;
    QString fileName;
    uint64_t downloadCount = 0;

    bool operator==(const ModInfo& other) const { return id == other.id; }
};

enum class LoadState { NotLoaded, Loaded, Errored };

struct VersionInfo {
    QString name;
    QString downloadUrl;
    QString fileName;
    uint64_t fileSize = 0;
    QString versionType; // release, beta, alpha
    QString sha1;
};

class ModListModel : public QAbstractListModel {
    Q_OBJECT
public:
    explicit ModListModel(QObject* parent = nullptr);
    int rowCount(const QModelIndex& parent) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    bool canFetchMore(const QModelIndex& parent) const override;
    void fetchMore(const QModelIndex& parent) override;

    void search(const QString& term, const QString& gameVersion, const QString& loader);
    void getVersions(const QString& projectId, const QString& gameVersion, const QString& loader);

    QList<VersionInfo> versions() const { return m_versions; }
    void reset();

signals:
    void searchFinished();
    void versionsFinished();
    void errorOccurred(const QString& message);

private slots:
    void onSearchFinished();
    void onSearchFailed();
    void onVersionsFinished();
    void onVersionsFailed();

private:
    void requestLogo(const QString& id, const QUrl& url);

    QVector<ModInfo> m_mods;
    QMap<QString, QIcon> m_logoMap;
    QStringList m_loadingLogos;
    QStringList m_failedLogos;

    QList<VersionInfo> m_versions;

    QString m_searchTerm;
    QString m_gameVersion;
    QString m_loader;
    int m_offset = 0;
    bool m_canFetchMore = false;
    bool m_searchInProgress = false;

    NetJob::Ptr m_searchJob;
    QByteArray m_searchResponse;
    NetJob::Ptr m_versionsJob;
    QByteArray m_versionsResponse;
};

} // namespace ModrinthModBrowserNS

Q_DECLARE_METATYPE(ModrinthModBrowserNS::ModInfo)

class ModrinthModBrowser : public QDialog
{
    Q_OBJECT
public:
    explicit ModrinthModBrowser(BaseInstance* instance, std::shared_ptr<ModFolderModel> modModel, QWidget* parent = nullptr);
    ~ModrinthModBrowser() override;

private slots:
    void triggerSearch();
    void onModSelected(const QModelIndex& index);
    void onVersionSelected(int index);
    void onDownloadClicked();
    void onSearchError(const QString& msg);

private:
    Ui::ModrinthModBrowser* ui;
    BaseInstance* m_instance;
    std::shared_ptr<ModFolderModel> m_modModel;
    ModrinthModBrowserNS::ModListModel* m_model;
    QString m_selectedModId;

    NetJob::Ptr m_downloadJob;
};
