#include "CurseForgeModBrowser.h"
#include "ui_CurseForgeModBrowser.h"

#include "Application.h"
#include "Json.h"
#include "modplatform/curseforge/CurseForgeAPI.h"
#include "net/Download.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"

#include <QMessageBox>
#include <QFileInfo>
#include <QIcon>
#include <QStandardPaths>
#include <QDir>

// ─── ModListModel ────────────────────────────────────────────────────────────

CurseForgeModBrowserNS::ModListModel::ModListModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int CurseForgeModBrowserNS::ModListModel::rowCount(const QModelIndex& parent) const
{
    return m_mods.size();
}

QVariant CurseForgeModBrowserNS::ModListModel::data(const QModelIndex& index, int role) const
{
    int pos = index.row();
    if (pos >= m_mods.size() || pos < 0 || !index.isValid())
        return {};

    const auto& mod = m_mods.at(pos);

    switch (role) {
        case Qt::DisplayRole:
            return mod.name;
        case Qt::ToolTipRole:
            return QString("%1\n%2\n%3")
                .arg(mod.name)
                .arg(mod.author)
                .arg(mod.description);
        case Qt::DecorationRole:
            if (m_logoMap.contains(mod.id))
                return m_logoMap.value(mod.id);
            {
                QIcon icon = APPLICATION->getThemedIcon("screenshot-placeholder");
                const_cast<ModListModel*>(this)->requestLogo(mod.id, mod.iconUrl);
                return icon;
            }
        case Qt::UserRole:
        {
            QVariant v;
            v.setValue(mod);
            return v;
        }
        default:
            return {};
    }
}

bool CurseForgeModBrowserNS::ModListModel::canFetchMore(const QModelIndex& parent) const
{
    return m_searchState == CanFetchMore;
}

void CurseForgeModBrowserNS::ModListModel::fetchMore(const QModelIndex& parent)
{
    if (parent.isValid())
        return;
    // Guard: QListView must never trigger the initial search — that's search()'s job.
    if (m_nextSearchOffset == 0) {
        return;
    }
    if (m_searchState != CanFetchMore)
        return;

    performPaginatedSearch();
}

void CurseForgeModBrowserNS::ModListModel::search(const QString& term, const QString& gameVersion, const QString& loader)
{
    m_searchGeneration++;

    // Abort in-flight search
    if (m_searchJob) {
        m_searchJob->abort();
        m_searchState = ResetRequested;
        // onSearchFailed will restart the search after abort completes
        return;
    }
    if (m_versionsJob) {
        m_versionsJob->abort();
        m_versionsJob.reset();
    }

    beginResetModel();
    m_mods.clear();
    m_versions.clear();
    m_nextSearchOffset = 0;
    m_searchState = None;
    m_searchTerm = term;
    m_gameVersion = gameVersion;
    m_loader = loader;
    m_searchResponse.clear();
    endResetModel();

    // Start initial search directly (NOT through fetchMore)
    performPaginatedSearch();
}

void CurseForgeModBrowserNS::ModListModel::performPaginatedSearch()
{
    QString apiKey = CurseForge::getApiKey();
    int gen = m_searchGeneration;

    // Build search URL with pagination and filters
    QString searchUrl = CurseForge::buildSearchUrl(
        QString(QUrl::toPercentEncoding(m_searchTerm)),
        CurseForge::CLASS_ID_MODS,
        static_cast<int>(CurseForge::SortFieldId::Popularity),
        "desc", 25, m_nextSearchOffset
    ).toString();

    // Append game version filter
    if (!m_gameVersion.isEmpty())
        searchUrl += "&gameVersion=" + QUrl::toPercentEncoding(m_gameVersion);

    // Append mod loader type filter
    if (!m_loader.isEmpty()) {
        int loaderTypeId = 0;
        if (m_loader == "Forge")
            loaderTypeId = static_cast<int>(CurseForge::ModLoaderTypeId::Forge);
        else if (m_loader == "Fabric")
            loaderTypeId = static_cast<int>(CurseForge::ModLoaderTypeId::Fabric);
        else if (m_loader == "NeoForge")
            loaderTypeId = static_cast<int>(CurseForge::ModLoaderTypeId::NeoForge);
        if (loaderTypeId > 0)
            searchUrl += QString("&modLoaderType=%1").arg(loaderTypeId);
    }

    m_searchResponse.clear();
    m_searchJob = NetJob::Ptr(new NetJob("CurseForge::ModSearch", APPLICATION->network()));
    auto dl = Net::Download::makeByteArray(QUrl(searchUrl), &m_searchResponse);
    CurseForge::addApiKeyHeader(dl.get(), apiKey);
    m_searchJob->addNetAction(dl);
    m_searchJob->start();

    QObject::connect(m_searchJob.get(), &NetJob::succeeded, this, [this, gen]() {
        if (gen == m_searchGeneration) onSearchSucceeded();
    });
    QObject::connect(m_searchJob.get(), &NetJob::failed, this, [this, gen]() {
        if (gen == m_searchGeneration) onSearchFailed();
    });
}

void CurseForgeModBrowserNS::ModListModel::onSearchSucceeded()
{
    m_searchJob.reset();

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(m_searchResponse, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        qWarning() << "Erro ao analisar resposta JSON do CurseForge em" << parse_error.offset
                   << "razão:" << parse_error.errorString();
        emit errorOccurred(tr("Erro ao analisar resposta da busca."));
        m_searchState = Finished;
        return;
    }

    QVector<ModInfo> newMods;
    int totalCount = 0;

    try {
        auto obj = Json::requireObject(doc);
        auto data = Json::requireArray(obj, "data");

        // Use requireObject to fail loudly if pagination is missing
        auto pagination = Json::requireObject(obj, "pagination");
        totalCount = Json::requireInteger(pagination, "totalCount");

        for (auto modRaw : data) {
            auto modObj = modRaw.toObject();
            ModInfo mod;
            try {
                mod.id = Json::requireInteger(modObj, "id");
                mod.name = Json::requireString(modObj, "name");
                mod.description = Json::ensureString(modObj, "summary", "");
                mod.downloadCount = modObj["downloadCount"].toVariant().toULongLong();

                // Extract author from authors array
                auto authors = modObj["authors"].toArray();
                if (!authors.isEmpty()) {
                    mod.author = Json::ensureString(authors[0].toObject(), "name", "Desconhecido");
                } else {
                    mod.author = "Desconhecido";
                }

                // Extract logo URL from nested logo object
                auto logoObj = Json::ensureObject(modObj, "logo", {});
                if (!logoObj.isEmpty()) {
                    mod.iconUrl = Json::ensureUrl(logoObj, "thumbnailUrl", QUrl());
                }

                newMods.append(mod);
            } catch (const JSONValidationError& e) {
                qWarning() << "Erro ao carregar mod do CurseForge:" << e.cause();
                continue;
            }
        }
    } catch (const JSONValidationError& e) {
        qWarning() << "Erro ao analisar resposta do CurseForge:" << e.cause();
        emit errorOccurred(tr("Erro ao analisar resposta da busca."));
        m_searchState = Finished;
        return;
    }

    // Pagination: same proven pattern as CurseForgeModel
    if ((totalCount - m_nextSearchOffset) <= 25)
        m_searchState = Finished;
    else {
        m_nextSearchOffset += 25;
        m_searchState = CanFetchMore;
    }

    beginInsertRows(QModelIndex(), m_mods.size(), m_mods.size() + newMods.size() - 1);
    for (const auto& item : newMods)
        m_mods.append(item);
    endInsertRows();

    emit searchFinished();
}

void CurseForgeModBrowserNS::ModListModel::onSearchFailed()
{
    m_searchJob.reset();

    // If a reset was requested while a search was in-flight, restart now
    if (m_searchState == ResetRequested) {
        beginResetModel();
        m_mods.clear();
        m_versions.clear();
        m_nextSearchOffset = 0;
        endResetModel();

        m_searchState = None;
        performPaginatedSearch();
        return;
    }

    m_searchState = Finished;
    emit errorOccurred(tr("Falha na busca. Verifique sua conexão com a internet e a chave de API."));
}

void CurseForgeModBrowserNS::ModListModel::getVersions(int modId, const QString& gameVersion, const QString& loader)
{
    if (m_versionsJob) {
        m_versionsJob->abort();
        m_versionsJob.reset();
    }

    m_versionsGeneration++;
    int gen = m_versionsGeneration;

    m_versions.clear();
    m_versionsResponse.clear();

    QString apiKey = CurseForge::getApiKey();

    // Build version list URL with game version filter
    QString versionsUrl = QString(
        "%1/mods/%2/files?gameVersion=%3&pageSize=50"
    ).arg(CurseForge::API_BASE).arg(modId).arg(gameVersion);

    m_versionsJob = NetJob::Ptr(new NetJob("CurseForge::ModVersions", APPLICATION->network()));
    auto dl = Net::Download::makeByteArray(QUrl(versionsUrl), &m_versionsResponse);
    CurseForge::addApiKeyHeader(dl.get(), apiKey);
    m_versionsJob->addNetAction(dl);

    QObject::connect(m_versionsJob.get(), &NetJob::succeeded, this, [this, gen]() {
        if (gen == m_versionsGeneration) onVersionsSucceeded();
    });
    QObject::connect(m_versionsJob.get(), &NetJob::failed, this, [this, gen]() {
        if (gen == m_versionsGeneration) onVersionsFailed();
    });
    m_versionsJob->start();
}

void CurseForgeModBrowserNS::ModListModel::reset()
{
    if (m_searchJob) {
        m_searchJob->abort();
        m_searchJob.reset();
    }
    if (m_versionsJob) {
        m_versionsJob->abort();
        m_versionsJob.reset();
    }

    beginResetModel();
    m_mods.clear();
    m_versions.clear();
    m_nextSearchOffset = 0;
    m_searchState = Finished;
    endResetModel();
}

void CurseForgeModBrowserNS::ModListModel::onVersionsSucceeded()
{
    m_versionsJob.reset();

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(m_versionsResponse, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        qWarning() << "Erro ao analisar versões do CurseForge:" << parse_error.errorString();
        emit errorOccurred(tr("Erro ao analisar lista de versões."));
        return;
    }

    try {
        auto obj = Json::requireObject(doc);
        QJsonArray data = Json::requireArray(obj, "data");
        for (auto verRaw : data) {
            auto verObj = verRaw.toObject();
            VersionInfo vi;
            try {
                vi.fileId = Json::requireInteger(verObj, "id");
                vi.displayName = Json::requireString(verObj, "displayName");
                vi.fileName = Json::requireString(verObj, "fileName");
                vi.downloadUrl = Json::ensureString(verObj, "downloadUrl", "");
                vi.fileSize = verObj["fileLength"].toVariant().toULongLong();
                vi.releaseType = Json::ensureInteger(verObj, "releaseType", 1);

                // Extract game version from gameVersions array
                auto gameVersions = verObj["gameVersions"].toArray();
                if (!gameVersions.isEmpty()) {
                    vi.gameVersion = gameVersions[0].toString();
                }

                // Extract MD5 hash from hashes array (algo 1 = MD5)
                auto hashes = verObj["hashes"].toArray();
                for (auto hashEntry : hashes) {
                    auto hashObj = hashEntry.toObject();
                    int algo = Json::ensureInteger(hashObj, "algo", 0);
                    if (algo == 1) {  // MD5
                        vi.md5 = Json::ensureString(hashObj, "value", "");
                        break;
                    }
                }

                if (!vi.downloadUrl.isEmpty())
                    m_versions.append(vi);
            } catch (const JSONValidationError& e) {
                qWarning() << "Erro ao carregar versão do CurseForge:" << e.cause();
                continue;
            }
        }
    } catch (const JSONValidationError& e) {
        qWarning() << "Erro ao analisar versões do CurseForge:" << e.cause();
        emit errorOccurred(tr("Erro ao analisar lista de versões."));
        return;
    }

    emit versionsFinished();
}

void CurseForgeModBrowserNS::ModListModel::onVersionsFailed()
{
    m_versionsJob.reset();
    m_versions.clear();
    emit errorOccurred(tr("Falha ao carregar versões. Verifique sua conexão e a chave de API."));
}

void CurseForgeModBrowserNS::ModListModel::requestLogo(int id, const QUrl& url)
{
    if (m_loadingLogos.contains(id) || m_failedLogos.contains(id) || !url.isValid() || url.scheme().isEmpty())
        return;

    MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry(
        "CurseForgeMods", QString("logos/%1").arg(id));

    auto job = NetJob::Ptr(new NetJob(QString("CurseForge Mod Icon %1").arg(id), APPLICATION->network()));
    job->addNetAction(Net::Download::makeCached(url, entry));

    auto fullPath = entry->getFullPath();
    QObject::connect(job.get(), &NetJob::succeeded, this, [this, id, fullPath, job]() mutable {
        QIcon icon(fullPath);
        if (icon.isNull()) {
            m_loadingLogos.removeAll(id);
            m_failedLogos.append(id);
            job.reset();
            return;
        }
        QSize size = icon.actualSize(QSize(48, 48));
        if (size.width() < 48 && size.height() < 48) {
            icon = icon.pixmap(48, 48).scaled(48, 48, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        }
        m_loadingLogos.removeAll(id);
        m_logoMap.insert(id, icon);
        for (int i = 0; i < m_mods.size(); i++) {
            if (m_mods[i].id == id) {
                emit dataChanged(createIndex(i, 0), createIndex(i, 0), {Qt::DecorationRole});
            }
        }
        job.reset();
    });

    QObject::connect(job.get(), &NetJob::failed, this, [this, id, job]() mutable {
        m_loadingLogos.removeAll(id);
        m_failedLogos.append(id);
        job.reset();
    });

    job->start();
    m_loadingLogos.append(id);
}

// ─── CurseForgeModBrowser ────────────────────────────────────────────────────

static QString detectLoader(MinecraftInstance* inst)
{
    auto profile = inst->getPackProfile();
    if (!profile)
        return {};

    if (profile->getComponent("net.minecraftforge"))
        return "Forge";
    if (profile->getComponent("net.fabric-loader"))
        return "Fabric";
    if (profile->getComponent("net.neoforged"))
        return "NeoForge";
    return {};
}

static QString detectGameVersion(MinecraftInstance* inst)
{
    auto profile = inst->getPackProfile();
    if (!profile)
        return {};
    auto mcComp = profile->getComponent("net.minecraft");
    if (!mcComp)
        return {};
    return mcComp->getVersion();
}

CurseForgeModBrowser::CurseForgeModBrowser(BaseInstance* instance, std::shared_ptr<ModFolderModel> modModel, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::CurseForgeModBrowser)
    , m_instance(instance)
    , m_modModel(modModel)
    , m_model(new CurseForgeModBrowserNS::ModListModel(this))
{
    ui->setupUi(this);

    auto mcInst = dynamic_cast<MinecraftInstance*>(instance);

    // Block signals during initialization to prevent spurious triggerSearch() calls
    ui->loaderComboBox->blockSignals(true);
    ui->versionComboBox->blockSignals(true);

    // Populate game version combo
    if (mcInst) {
        QString gameVer = detectGameVersion(mcInst);
        if (!gameVer.isEmpty()) {
            ui->versionComboBox->addItem(gameVer);
            ui->versionComboBox->setCurrentIndex(0);
            ui->versionComboBox->setEnabled(false); // locked to instance version
            ui->versionComboBox->setToolTip(tr("Versão do Minecraft da instância: %1").arg(gameVer));
        }

        // Populate loader combo
        QString loader = detectLoader(mcInst);
        ui->loaderComboBox->addItem(tr("Todos"), "");
        ui->loaderComboBox->addItem("Forge", "Forge");
        ui->loaderComboBox->addItem("Fabric", "Fabric");
        ui->loaderComboBox->addItem("NeoForge", "NeoForge");

        if (!loader.isEmpty()) {
            int idx = ui->loaderComboBox->findData(loader);
            if (idx >= 0) {
                ui->loaderComboBox->setCurrentIndex(idx);
                ui->loaderComboBox->setEnabled(false); // locked to instance loader
                ui->loaderComboBox->setToolTip(tr("Loader da instância: %1").arg(loader));
            }
        }
    }

    // Unblock signals after initialization
    ui->loaderComboBox->blockSignals(false);
    ui->versionComboBox->blockSignals(false);

    // Set up mod list view
    ui->modListView->setModel(m_model);
    ui->modListView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    ui->modListView->setAlternatingRowColors(true);
    ui->modListView->setIconSize(QSize(48, 48));

    // Connect signals
    connect(ui->searchButton, &QPushButton::clicked, this, &CurseForgeModBrowser::triggerSearch);
    connect(ui->searchEdit, &QLineEdit::returnPressed, this, &CurseForgeModBrowser::triggerSearch);

    connect(ui->modListView, &QListView::clicked, this, &CurseForgeModBrowser::onModSelected);
    connect(ui->modListView, &QListView::doubleClicked, this, &CurseForgeModBrowser::onModSelected);

    connect(ui->versionComboBox, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, &CurseForgeModBrowser::onVersionSelected);
    connect(ui->loaderComboBox, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
            this, [this](int) { triggerSearch(); });

    connect(ui->downloadButton, &QPushButton::clicked, this, &CurseForgeModBrowser::onDownloadClicked);
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_model, &CurseForgeModBrowserNS::ModListModel::searchFinished, this, [this]() {
        ui->statusLabel->setText(tr("%1 mods encontrados.").arg(m_model->rowCount(QModelIndex())));
        ui->downloadButton->setEnabled(false);
        ui->versionComboBox->clear();

        // Restore game version item if it was removed by clearing
        auto mcInst2 = dynamic_cast<MinecraftInstance*>(m_instance);
        if (mcInst2) {
            QString gameVer = detectGameVersion(mcInst2);
            if (!gameVer.isEmpty()) {
                ui->versionComboBox->addItem(gameVer);
                ui->versionComboBox->setEnabled(false);
            }
        }
    });

    connect(m_model, &CurseForgeModBrowserNS::ModListModel::versionsFinished, this, [this]() {
        ui->versionComboBox->clear();
        auto versions = m_model->versions();

        if (versions.isEmpty()) {
            ui->statusLabel->setText(tr("Nenhuma versão compatível encontrada."));
            ui->downloadButton->setEnabled(false);
            return;
        }

        // Restore game version item
        auto mcInst2 = dynamic_cast<MinecraftInstance*>(m_instance);
        if (mcInst2) {
            QString gameVer = detectGameVersion(mcInst2);
            if (!gameVer.isEmpty()) {
                ui->versionComboBox->addItem(gameVer);
            }
        }

        for (int i = 0; i < versions.size(); i++) {
            const auto& v = versions[i];
            QString label = v.displayName;
            if (v.releaseType == 2)
                label += QString(" [%1]").arg(tr("Beta"));
            else if (v.releaseType == 3)
                label += QString(" [%1]").arg(tr("Alpha"));
            else
                label += QString(" [%1]").arg(tr("Release"));
            ui->versionComboBox->addItem(label);
        }

        ui->downloadButton->setEnabled(true);
        ui->statusLabel->setText(tr("%1 versão(ões) disponível(is).").arg(versions.size()));
    });

    connect(m_model, &CurseForgeModBrowserNS::ModListModel::errorOccurred, this, &CurseForgeModBrowser::onSearchError);

    // Disable download button initially
    ui->downloadButton->setEnabled(false);
    ui->statusLabel->setText(tr("Digite um termo de busca para encontrar mods."));
}

CurseForgeModBrowser::~CurseForgeModBrowser()
{
    delete ui;
}

void CurseForgeModBrowser::triggerSearch()
{
    QString term = ui->searchEdit->text().trimmed();
    auto mcInst = dynamic_cast<MinecraftInstance*>(m_instance);

    QString gameVersion;
    QString loader;

    if (mcInst) {
        gameVersion = detectGameVersion(mcInst);
        loader = ui->loaderComboBox->currentData().toString();
    }

    ui->statusLabel->setText(tr("Buscando..."));
    m_selectedModId = 0;
    ui->downloadButton->setEnabled(false);

    m_model->search(term, gameVersion, loader);
}

void CurseForgeModBrowser::onModSelected(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    auto mcInst = dynamic_cast<MinecraftInstance*>(m_instance);
    if (!mcInst)
        return;

    QString gameVersion = detectGameVersion(mcInst);

    // Get mod info from model
    QVariant data = m_model->data(index, Qt::UserRole);
    if (data.canConvert<CurseForgeModBrowserNS::ModInfo>()) {
        auto mod = data.value<CurseForgeModBrowserNS::ModInfo>();
        m_selectedModId = mod.id;
        ui->statusLabel->setText(tr("Carregando versões de '%1'...").arg(mod.name));
        ui->downloadButton->setEnabled(false);
        m_model->getVersions(mod.id, gameVersion, {});
    }
}

void CurseForgeModBrowser::onVersionSelected(int index)
{
    // Version combo first item is the game version info (disabled), actual versions start at index 1
    Q_UNUSED(index);
}

void CurseForgeModBrowser::onDownloadClicked()
{
    auto mcInst = dynamic_cast<MinecraftInstance*>(m_instance);
    if (!mcInst) {
        QMessageBox::warning(this, tr("Erro"), tr("Instância inválida."));
        return;
    }

    auto versions = m_model->versions();
    // The game version entry is at index 0, versions start at index 1
    int versionIndex = ui->versionComboBox->currentIndex() - 1;
    if (versionIndex < 0 || versionIndex >= versions.size()) {
        QMessageBox::warning(this, tr("Erro"), tr("Selecione uma versão para baixar."));
        return;
    }

    const auto& version = versions[versionIndex];
    if (version.downloadUrl.isEmpty()) {
        QMessageBox::warning(this, tr("Erro"), tr("URL de download inválida."));
        return;
    }

    // Determine target path in the instance's mod folder
    QString modDir = m_modModel->dir().absolutePath();
    QString targetPath = QDir(modDir).absoluteFilePath(version.fileName);

    // Check if file already exists
    if (QFileInfo::exists(targetPath)) {
        auto result = QMessageBox::question(
            this, tr("Arquivo existe"),
            tr("O arquivo '%1' já existe na pasta de mods.\nDeseja substituí-lo?").arg(version.fileName),
            QMessageBox::Yes | QMessageBox::No);
        if (result != QMessageBox::Yes)
            return;
        QFile::remove(targetPath);
    }

    ui->downloadButton->setEnabled(false);
    ui->statusLabel->setText(tr("Baixando '%1'...").arg(version.fileName));

    // Use Net::Download::makeFile to download the mod file
    m_downloadJob = NetJob::Ptr(new NetJob("CurseForge::DownloadMod", APPLICATION->network()));
    m_downloadJob->addNetAction(Net::Download::makeFile(QUrl(version.downloadUrl), targetPath));

    QObject::connect(m_downloadJob.get(), &NetJob::succeeded, this, [this]() {
        m_downloadJob.reset();
        ui->statusLabel->setText(tr("Download concluído!"));
        ui->downloadButton->setEnabled(true);
        m_modModel->update();
    });

    QObject::connect(m_downloadJob.get(), &NetJob::failed, this, [this](const QString& reason) {
        m_downloadJob.reset();
        ui->statusLabel->setText(tr("Falha no download: %1").arg(reason));
        ui->downloadButton->setEnabled(true);
    });

    QObject::connect(m_downloadJob.get(), &NetJob::progress, this, [this](qint64 current, qint64 total) {
        if (total > 0) {
            double percent = (current * 100.0) / total;
            ui->statusLabel->setText(tr("Baixando... %1%").arg(QString::number(percent, 'f', 1)));
        }
    });

    m_downloadJob->start();
}

void CurseForgeModBrowser::onSearchError(const QString& msg)
{
    ui->statusLabel->setText(msg);
}
