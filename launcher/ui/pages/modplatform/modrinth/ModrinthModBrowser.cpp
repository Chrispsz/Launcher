#include "ModrinthModBrowser.h"
#include "ui_ModrinthModBrowser.h"

#include "Application.h"
#include "Json.h"
#include "minecraft/MinecraftInstance.h"
#include "minecraft/PackProfile.h"
#include "minecraft/mod/ModFolderModel.h"

#include <QMessageBox>
#include <QFileInfo>
#include <QOverload>
#include <QIcon>
#include <QStandardPaths>
#include <QDir>

// ─── ModListModel ────────────────────────────────────────────────────────────

ModrinthModBrowserNS::ModListModel::ModListModel(QObject* parent)
    : QAbstractListModel(parent)
{}

int ModrinthModBrowserNS::ModListModel::rowCount(const QModelIndex& parent) const
{
    return m_mods.size();
}

QVariant ModrinthModBrowserNS::ModListModel::data(const QModelIndex& index, int role) const
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

bool ModrinthModBrowserNS::ModListModel::canFetchMore(const QModelIndex& parent) const
{
    return m_canFetchMore;
}

void ModrinthModBrowserNS::ModListModel::fetchMore(const QModelIndex& parent)
{
    if (parent.isValid() || !m_canFetchMore || m_searchInProgress)
        return;

    // Build search URL with pagination
    QString facets;
    facets += "[[\"project_type:mod\"]";

    if (!m_gameVersion.isEmpty()) {
        facets += QString(",[\"versions:%1\"]").arg(m_gameVersion);
    }
    if (!m_loader.isEmpty()) {
        facets += QString(",[\"categories:%1\"]").arg(m_loader);
    }
    facets += "]";

    QString searchUrl = QString(
        "https://api.modrinth.com/v2/search?facets=%1&query=%2&limit=25&offset=%3&index=relevance"
    ).arg(QString(QUrl::toPercentEncoding(facets)))
     .arg(QString(QUrl::toPercentEncoding(m_searchTerm)))
     .arg(m_offset);

    m_searchJob = new NetJob("Modrinth::ModSearch", APPLICATION->network());
    m_searchJob->addNetAction(Net::Download::makeByteArray(QUrl(searchUrl), &m_searchResponse));
    m_searchInProgress = true;

    QObject::connect(m_searchJob.get(), &NetJob::succeeded, this, &ModListModel::onSearchFinished);
    QObject::connect(m_searchJob.get(), &NetJob::failed, this, &ModListModel::onSearchFailed);
    m_searchJob->start();
}

void ModrinthModBrowserNS::ModListModel::search(const QString& term, const QString& gameVersion, const QString& loader)
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
    m_canFetchMore = false;
    m_searchInProgress = false;
    m_searchTerm = term;
    m_gameVersion = gameVersion;
    m_loader = loader;
    endResetModel();

    // Trigger initial fetch
    fetchMore(QModelIndex());
}

void ModrinthModBrowserNS::ModListModel::getVersions(const QString& projectId, const QString& gameVersion, const QString& loader)
{
    if (m_versionsJob) {
        m_versionsJob->abort();
        m_versionsJob.reset();
    }

    m_versions.clear();
    m_versionsResponse.clear();

    // Build version list URL with optional filters
    QString versionsUrl = QString("https://api.modrinth.com/v2/project/%1/version").arg(projectId);

    QStringList queryParts;
    if (!gameVersion.isEmpty()) {
        queryParts << QString("game_versions=[\"%1\"]").arg(gameVersion);
    }
    if (!loader.isEmpty()) {
        queryParts << QString("loaders=[\"%1\"]").arg(loader);
    }
    if (!queryParts.isEmpty()) {
        versionsUrl += "?" + queryParts.join("&");
    }

    m_versionsJob = new NetJob("Modrinth::ModVersions", APPLICATION->network());
    m_versionsJob->addNetAction(Net::Download::makeByteArray(QUrl(versionsUrl), &m_versionsResponse));

    QObject::connect(m_versionsJob.get(), &NetJob::succeeded, this, &ModListModel::onVersionsFinished);
    QObject::connect(m_versionsJob.get(), &NetJob::failed, this, &ModListModel::onVersionsFailed);
    m_versionsJob->start();
}

void ModrinthModBrowserNS::ModListModel::reset()
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
    m_canFetchMore = false;
    m_searchInProgress = false;
    endResetModel();
}

void ModrinthModBrowserNS::ModListModel::onSearchFinished()
{
    m_searchJob.reset();
    m_searchInProgress = false;

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(m_searchResponse, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        qWarning() << "Erro ao analisar resposta JSON do Modrinth em" << parse_error.offset
                   << "razão:" << parse_error.errorString();
        emit errorOccurred(tr("Erro ao analisar resposta da busca."));
        return;
    }

    QVector<ModInfo> newMods;
    int totalHits = 0;

    try {
        auto obj = Json::requireObject(doc);
        auto hits = Json::requireArray(obj, "hits");
        totalHits = Json::requireInteger(obj, "total_hits");

        for (auto hitRaw : hits) {
            auto hitObj = hitRaw.toObject();
            ModInfo mod;
            try {
                mod.id = Json::requireString(hitObj, "project_id");
                mod.name = Json::requireString(hitObj, "title");
                mod.description = Json::ensureString(hitObj, "description", "");
                mod.author = Json::ensureString(hitObj, "author", "Desconhecido");
                mod.iconUrl = Json::requireUrl(hitObj, "icon_url");
                mod.downloadCount = Json::ensureInteger(hitObj, "downloads", 0);
                newMods.append(mod);
            } catch (const JSONValidationError& e) {
                qWarning() << "Erro ao carregar mod do Modrinth:" << e.cause();
                continue;
            }
        }
    } catch (const JSONValidationError& e) {
        qWarning() << "Erro ao analisar resposta do Modrinth:" << e.cause();
        emit errorOccurred(tr("Erro ao analisar resposta da busca."));
        return;
    }

    if ((totalHits - m_offset) <= 25)
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

void ModrinthModBrowserNS::ModListModel::onSearchFailed()
{
    m_searchJob.reset();
    m_searchInProgress = false;
    m_canFetchMore = false;
    emit errorOccurred(tr("Falha na busca. Verifique sua conexão com a internet."));
}

void ModrinthModBrowserNS::ModListModel::onVersionsFinished()
{
    m_versionsJob.reset();

    QJsonParseError parse_error;
    QJsonDocument doc = QJsonDocument::fromJson(m_versionsResponse, &parse_error);
    if (parse_error.error != QJsonParseError::NoError) {
        qWarning() << "Erro ao analisar versões do Modrinth:" << parse_error.errorString();
        emit errorOccurred(tr("Erro ao analisar lista de versões."));
        return;
    }

    try {
        QJsonArray versions = Json::requireArray(doc);
        for (auto verRaw : versions) {
            auto verObj = verRaw.toObject();
            VersionInfo vi;
            try {
                vi.name = Json::requireString(verObj, "version_number");
                vi.versionType = Json::ensureString(verObj, "version_type", "release");

                // Get primary file
                auto files = Json::requireArray(verObj, "files");
                bool found = false;
                for (int i = 0; i < files.size(); i++) {
                    auto fileObj = files[i].toObject();
                    bool primary = Json::ensureBoolean(fileObj, "primary", false);
                    vi.fileName = Json::requireString(fileObj, "filename");
                    vi.downloadUrl = Json::requireString(fileObj, "url");
                    vi.fileSize = Json::ensureInteger(fileObj, "size", 0);

                    auto hashes = fileObj["hashes"].toObject();
                    vi.sha1 = Json::ensureString(hashes, "sha1", "");

                    if (primary || i == files.size() - 1) {
                        found = true;
                        break;
                    }
                }

                if (found)
                    m_versions.append(vi);
            } catch (const JSONValidationError& e) {
                qWarning() << "Erro ao carregar versão do Modrinth:" << e.cause();
                continue;
            }
        }
    } catch (const JSONValidationError& e) {
        qWarning() << "Erro ao analisar versões do Modrinth:" << e.cause();
        emit errorOccurred(tr("Erro ao analisar lista de versões."));
        return;
    }

    emit versionsFinished();
}

void ModrinthModBrowserNS::ModListModel::onVersionsFailed()
{
    m_versionsJob.reset();
    m_versions.clear();
    emit errorOccurred(tr("Falha ao carregar versões. Verifique sua conexão."));
}

void ModrinthModBrowserNS::ModListModel::requestLogo(const QString& id, const QUrl& url)
{
    if (m_loadingLogos.contains(id) || m_failedLogos.contains(id) || url.isEmpty())
        return;

    MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry(
        "ModrinthMods", QString("logos/%1").arg(id.section(".", 0, 0)));

    auto* job = new NetJob(QString("Modrinth Mod Icon %1").arg(id), APPLICATION->network());
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

// ─── ModrinthModBrowser ──────────────────────────────────────────────────────

static QString detectLoader(MinecraftInstance* inst)
{
    auto profile = inst->getPackProfile();
    if (!profile)
        return {};

    if (profile->getComponent("net.minecraftforge"))
        return "forge";
    if (profile->getComponent("net.fabric-loader"))
        return "fabric";
    if (profile->getComponent("net.neoforged"))
        return "neoforge";
    if (profile->getComponent("org.quiltmc.loader"))
        return "quilt";

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

ModrinthModBrowser::ModrinthModBrowser(BaseInstance* instance, std::shared_ptr<ModFolderModel> modModel, QWidget* parent)
    : QDialog(parent)
    , ui(new Ui::ModrinthModBrowser)
    , m_instance(instance)
    , m_modModel(modModel)
    , m_model(new ModrinthModBrowserNS::ModListModel(this))
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
        ui->loaderComboBox->addItem("Forge", "forge");
        ui->loaderComboBox->addItem("Fabric", "fabric");
        ui->loaderComboBox->addItem("NeoForge", "neoforge");
        ui->loaderComboBox->addItem("Quilt", "quilt");

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
    connect(ui->searchButton, &QPushButton::clicked, this, &ModrinthModBrowser::triggerSearch);
    connect(ui->searchEdit, &QLineEdit::returnPressed, this, &ModrinthModBrowser::triggerSearch);

    connect(ui->modListView, &QListView::clicked, this, &ModrinthModBrowser::onModSelected);
    connect(ui->modListView, &QListView::doubleClicked, this, &ModrinthModBrowser::onModSelected);

    connect(ui->versionComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, &ModrinthModBrowser::onVersionSelected);
    connect(ui->loaderComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged),
            this, [this](int) { triggerSearch(); });

    connect(ui->downloadButton, &QPushButton::clicked, this, &ModrinthModBrowser::onDownloadClicked);
    connect(ui->closeButton, &QPushButton::clicked, this, &QDialog::reject);

    connect(m_model, &ModrinthModBrowserNS::ModListModel::searchFinished, this, [this]() {
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

    connect(m_model, &ModrinthModBrowserNS::ModListModel::versionsFinished, this, [this]() {
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
            QString label = v.name;
            if (!v.versionType.isEmpty()) {
                if (v.versionType == "beta")
                    label += QString(" [%1]").arg(tr("Beta"));
                else if (v.versionType == "alpha")
                    label += QString(" [%1]").arg(tr("Alpha"));
                else
                    label += QString(" [%1]").arg(tr("Release"));
            }
            ui->versionComboBox->addItem(label);
        }

        ui->downloadButton->setEnabled(true);
        ui->statusLabel->setText(tr("%1 versão(ões) disponível(is).").arg(versions.size()));
    });

    connect(m_model, &ModrinthModBrowserNS::ModListModel::errorOccurred, this, &ModrinthModBrowser::onSearchError);

    // Disable download button initially
    ui->downloadButton->setEnabled(false);
    ui->statusLabel->setText(tr("Digite um termo de busca para encontrar mods."));
}

ModrinthModBrowser::~ModrinthModBrowser()
{
    delete ui;
}

void ModrinthModBrowser::triggerSearch()
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
    m_selectedModId.clear();
    ui->downloadButton->setEnabled(false);

    m_model->search(term, gameVersion, loader);
}

void ModrinthModBrowser::onModSelected(const QModelIndex& index)
{
    if (!index.isValid())
        return;

    auto mcInst = dynamic_cast<MinecraftInstance*>(m_instance);
    if (!mcInst)
        return;

    QString gameVersion = detectGameVersion(mcInst);
    QString loader = ui->loaderComboBox->currentData().toString();

    // Get mod info from model
    QVariant data = m_model->data(index, Qt::UserRole);
    if (data.canConvert<ModrinthModBrowserNS::ModInfo>()) {
        auto mod = data.value<ModrinthModBrowserNS::ModInfo>();
        m_selectedModId = mod.id;
        ui->statusLabel->setText(tr("Carregando versões de '%1'...").arg(mod.name));
        ui->downloadButton->setEnabled(false);
        m_model->getVersions(mod.id, gameVersion, loader);
    }
}

void ModrinthModBrowser::onVersionSelected(int index)
{
    // Version combo first item is the game version info (disabled), actual versions start at index 1
    Q_UNUSED(index);
}

void ModrinthModBrowser::onDownloadClicked()
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
    m_downloadJob = new NetJob("Modrinth::DownloadMod", APPLICATION->network());
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

void ModrinthModBrowser::onSearchError(const QString& msg)
{
    ui->statusLabel->setText(msg);
}
