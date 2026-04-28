#pragma once

#include <QDialog>
#include <QAbstractListModel>
#include "net/NetJob.h"

class ModFolderModel;
class BaseInstance;

namespace Ui {
    class CurseForgeModBrowser;
}

namespace CurseForgeModBrowserNS {

struct ModInfo {
    int id = 0;
    QString name;
    QString author;
    QString description;
    QUrl iconUrl;
    uint64_t downloadCount = 0;

    bool operator==(const ModInfo& other) const { return id == other.id; }
};

struct VersionInfo {
    int fileId = 0;
    QString displayName;
    QString downloadUrl;
    QString fileName;
    uint64_t fileSize = 0;
    QString md5;
    QString gameVersion;
    int releaseType = 1;  // 1=Release, 2=Beta, 3=Alpha
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
    void getVersions(int modId, const QString& gameVersion, const QString& loader);

    QList<VersionInfo> versions() const { return m_versions; }
    void reset();

signals:
    void searchFinished();
    void versionsFinished();
    void errorOccurred(const QString& message);

private slots:
    void onSearchSucceeded();
    void onSearchFailed();
    void onVersionsSucceeded();
    void onVersionsFailed();

private:
    void performPaginatedSearch();
    void requestLogo(int id, const QUrl& url);

    QVector<ModInfo> m_mods;
    QMap<int, QIcon> m_logoMap;
    QList<int> m_loadingLogos;
    QList<int> m_failedLogos;

    QList<VersionInfo> m_versions;

    QString m_searchTerm;
    QString m_gameVersion;
    QString m_loader;
    int m_nextSearchOffset = 0;
    int m_searchGeneration = 0;
    int m_versionsGeneration = 0;

    // Proven state machine from CurseForgeModel (modpack browser)
    enum SearchState {
        None,
        CanFetchMore,
        Finished
    } m_searchState = None;

    NetJob::Ptr m_searchJob;
    QByteArray m_searchResponse;
    NetJob::Ptr m_versionsJob;
    QByteArray m_versionsResponse;
};

} // namespace CurseForgeModBrowserNS

Q_DECLARE_METATYPE(CurseForgeModBrowserNS::ModInfo)

class CurseForgeModBrowser : public QDialog
{
    Q_OBJECT
public:
    explicit CurseForgeModBrowser(BaseInstance* instance, std::shared_ptr<ModFolderModel> modModel, QWidget* parent = nullptr);
    ~CurseForgeModBrowser() override;

private slots:
    void triggerSearch();
    void onModSelected(const QModelIndex& index);
    void onVersionSelected(int index);
    void onDownloadClicked();
    void onSearchError(const QString& msg);

private:
    void startModDownload(const QUrl& url, const QString& targetPath);

    Ui::CurseForgeModBrowser* ui;
    BaseInstance* m_instance;
    std::shared_ptr<ModFolderModel> m_modModel;
    CurseForgeModBrowserNS::ModListModel* m_model;
    int m_selectedModId = 0;

    NetJob::Ptr m_downloadJob;
};
