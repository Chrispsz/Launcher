#include "CurseForgeModBrowser.h"
#include "ui_CurseForgeModBrowser.h"

#include "Application.h"
#include "Json.h"
#include "modplatform/curseforge/CurseForgeAPI.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"

#include <QMessageBox>
#include <QFileInfo>
#include <QIcon>
#include <QOverload>
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
    return m_canFetchMore;
}

void CurseForgeModBrowserNS::ModListModel::fetchMore(const QModelIndex& parent)
{
    if (parent.isValid() || !m_canFetchMore || m_searchInProgress)
        return;

    QString apiKey = CurseForge::getApiKey();

    // Build search URL with pagination
    QString searchUrl = QString(
        "https://api.curseforge.com/v1/mods/search?gameId=432&classId=6"
        "&searchFilter=%1&sortField=2&sortOrder=desc&pageSize=25&indexOffset=%2"
        "&apiKey=%3"
    ).arg(QString(QUrl::toPercentEncoding(m_searchTerm)))
     .arg(m_offset)
     .arg(apiKey);

    m_searchJob = new NetJob("CurseForge::ModSearch", APPLICATION->network());
    m_searchJob->addNetAction(Net::Download::makeByteArray(QUrl(searchUrl), &m_searchResponse));
    m_searchInProgress = true;

    QObject::connect(m_searchJob.get(), &NetJob::succeeded, this, &ModListModel::onSearchFinished);
    QObject::connect(m_searchJob.get(), &NetJob::failed, this, &ModListModel::onSearchFailed);
    m_searchJob->start();
}

void CurseForgeModBrowserNS::ModListModel::search(const QString& term, const QString& gameVersion, const QString& loader)
{
    // Abort in-flight search
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
    m_offset = 0;
    m_totalCount = 0;
    m_canFetchMore = false;
    m_searchInProgress = false;
    m_searchTerm = term;
    m_gameVersion = gameVersion;
    m_loader = loader;
    endResetModel();

    // Trigger initial fetch
    fetchMore(QModelIndex());
}

void CurseForgeModBrowserNS::ModListModel::getVersions(int modId, const QString& gameVersion, const QString& loader)
{
    if (m_versionsJob) {
        m_versionsJob->abort();
        m_versionsJob.reset();
    }

    m_versions.clear();
    m_versionsResponse.clear();

    QString apiKey = CurseForge::getApiKey();

    // Build version list URL with game version filter
    QString versionsUrl = QString(
        "https://api.curseforge.com/v1/mods/%1/files?gameVersion=%2&pageSize=50&apiKey=%3"
    ).arg(modId).arg(gameVersion).arg(apiKey);

    m_versionsJob = new NetJob("CurseForge::ModVersions", APPLICATION->network());
    m_versionsJob->addNetAction(Net::Download::makeByteArray(QUrl(versionsUrl), &m_versionsResponse));

    QObject::connect(m_versionsJob.get(), &NetJob::succeeded, this, &ModListModel::onVersionsFinished);
    QObject::connect(m_versionsJob.get(), &NetJob::failed, this, &ModListModel::onVersionsFailed);
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
    m_offset = 0;
    m_totalCount = 0;
    m_canFetchMore = false;
    m_searchInProgress = false;
    endResetModel();
}

void CurseForgeModBrowserNS::ModListModel::onSearchFinished()
{
    m_searchJob.reset();
    m_searchInProgress = false;

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(m_searchResponse, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        qWarning() << "Erro ao analisar resposta JSON do CurseForge em" << parse_error.offset
                   << "razão:" << parse_error.errorString();
        emit errorOccurred(tr("Erro ao analisar resposta da busca."));
        return;
    }

    QVector<ModInfo> newMods;
    int totalCount = 0;

    try {
        auto obj = Json::requireObject(doc);
        auto data = Json::requireArray(obj, "data");

        auto pagination = obj["pagination"].toObject();
        totalCount = Json::ensureInteger(pagination, "totalCount", 0);

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

                // Extract logo URL
                auto logo = modObj["logo"].toObject();
                if (!logo.isEmpty()) {
                    mod.iconUrl = QUrl(Json::ensureString(logo, "thumbnailUrl", ""));
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
        return;
    }

    m_totalCount = totalCount;

    if ((m_offset + 25) >= totalCount)
        m_canFetchMore = false;
    else {
        m_offset += 25;
        m_canFetchMore = true;
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
    m_searchInProgress = false;
    m_canFetchMore = false;
    emit errorOccurred(tr("Falha na busca. Verifique sua conexão com a internet e a chave de API."));
}

void CurseForgeModBrowserNS::ModListModel::onVersionsFinished()
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
                    // First element is usually the Minecraft version
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
    if (m_loadingLogos.contains(id) || m_failedLogos.contains(id) || url.isEmpty())
        return;

    MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry(
        "CurseForgeMods", QString("logos/%1").arg(id));

    auto* job = new NetJob(QString("CurseForge Mod Icon %1").arg(id), APPLICATION->network());
    job->addNetAction(Net::Download::makeCached(url, entry));

    auto fullPath = entry->getFullPath();
    QObject::connect(job, &NetJob::succeeded, this, [this, id, fullPath] {
        QIcon icon(fullPath);
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
    });

    QObject::connect(job, &NetJob::failed, this, [this, id] {
        m_loadingLogos.removeAll(id);
        m_failedLogos.append(id);
    });

    job->start();
    m_loadingLogos.append(id);
}

QString CurseForgeModBrowserNS::ModListModel::getApiKey() const
{
    return CurseForge::getApiKey();
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
    if (profile->getComponent("org.quiltmc.loader"))
        return "Quilt";

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
        ui->loaderComboBox->addItem("Quilt", "Quilt");

        if (!loader.isEmpty()) {
            int idx = ui->loaderComboBox->findData(loader);
            if (idx >= 0) {
                ui->loaderComboBox->setCurrentIndex(idx);
                ui->loaderComboBox->setEnabled(false); // locked to instance loader
                ui->loaderComboBox->setToolTip(tr("Loader da instância: %1").arg(loader));
            }
        }
    }

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

    connect(ui->versionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &CurseForgeModBrowser::onVersionSelected);
    connect(ui->loaderComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
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
    m_downloadJob = new NetJob("CurseForge::DownloadMod", APPLICATION->network());
    m_downloadJob->addNetAction(Net::Download::makeFile(QUrl(version.downloadUrl), targetPath));

    connect(m_downloadJob.get(), &NetJob::succeeded, this, [this]() {
        m_downloadJob.reset();
        ui->statusLabel->setText(tr("Download concluído!"));
        ui->downloadButton->setEnabled(true);
        m_modModel->update();
    });

    connect(m_downloadJob.get(), &NetJob::failed, this, [this](const QString& reason) {
        m_downloadJob.reset();
        ui->statusLabel->setText(tr("Falha no download: %1").arg(reason));
        ui->downloadButton->setEnabled(true);
    });

    connect(m_downloadJob.get(), &NetJob::progress, this, [this](qint64 current, qint64 total) {
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
