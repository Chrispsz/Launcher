#include "CurseForgePage.h"
#include "CurseForgeModel.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "ui_CurseForgePage.h"

#include <QKeyEvent>
#include <QTextDocument>
#include <InstanceImportTask.h>

CurseForgePage::CurseForgePage(NewInstanceDialog *dialog, QWidget *parent)
    : QWidget(parent), ui(new Ui::CurseForgePage), dialog(dialog)
{
    ui->setupUi(this);
    connect(ui->searchButton, &QPushButton::clicked, this, &CurseForgePage::triggerSearch);
    ui->searchEdit->installEventFilter(this);
    model = new CurseForge::ListModel(this);
    ui->packView->setModel(model);

    ui->versionSelectionBox->view()->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    ui->versionSelectionBox->view()->parentWidget()->setMaximumHeight(300);

    ui->sortByBox->addItem(tr("Ordenar por popularidade"), 2);
    ui->sortByBox->addItem(tr("Ordenar por última atualização"), 3);
    ui->sortByBox->addItem(tr("Ordenar por nome"), 4);
    ui->sortByBox->addItem(tr("Ordenar por total de downloads"), 6);

    connect(ui->sortByBox, SIGNAL(currentIndexChanged(int)), this, SLOT(triggerSearch()));
    connect(ui->packView->selectionModel(), &QItemSelectionModel::currentChanged, this, &CurseForgePage::onSelectionChanged);
    connect(ui->versionSelectionBox, &QComboBox::currentTextChanged, this, &CurseForgePage::onVersionSelectionChanged);
    connect(model, &CurseForge::ListModel::packDataChanged, this, &CurseForgePage::onPackDataChanged);
}

CurseForgePage::~CurseForgePage()
{
    delete ui;
}

void CurseForgePage::openedImpl()
{
    BasePage::openedImpl();
    triggerSearch();
}

bool CurseForgePage::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == ui->searchEdit && event->type() == QEvent::KeyPress) {
        auto *keyEvent = reinterpret_cast<QKeyEvent *>(event);
        if (keyEvent->key() == Qt::Key_Return) {
            this->triggerSearch();
            keyEvent->accept();
            return true;
        }
    }
    return QObject::eventFilter(watched, event);
}

void CurseForgePage::triggerSearch()
{
    model->searchWithTerm(ui->searchEdit->text(), ui->sortByBox->itemData(ui->sortByBox->currentIndex()).toInt());
}

void CurseForgePage::onSelectionChanged(QModelIndex first, QModelIndex second)
{
    if(!first.isValid())
    {
        if(isOpened)
        {
            dialog->setSuggestedPack();
        }
        return;
    }

    current = model->data(first, Qt::UserRole).value<CurseForge::Modpack>();
    model->getPackDetails(current.id);
    updateCurrentPackUI();
    suggestCurrent();
}

void CurseForgePage::onVersionSelectionChanged(const QString& version)
{
    if(version.isEmpty() || ui->versionSelectionBox->count() == 0) {
        currentVersion = CurseForge::ModVersion();
    }
    else {
        currentVersion = ui->versionSelectionBox->currentData().value<CurseForge::ModVersion>();
    }
    suggestCurrent();
}

void CurseForgePage::suggestCurrent()
{
    if(!isOpened)
    {
        return;
    }

    if (!currentVersion.download.valid)
    {
        dialog->setSuggestedPack();
        return;
    }

    dialog->setSuggestedPack(current.name + " " + currentVersion.displayName, new InstanceImportTask(QUrl(currentVersion.download.url)));
    MetaEntryPtr entry = APPLICATION->metacache()->resolveEntry("CurseForgePacks", QString("logos/%1").arg(current.id));
    dialog->setSuggestedIconFromFile(entry->getFullPath(), QString("curseforge-%1").arg(current.id));
}

void CurseForgePage::onPackDataChanged(int id)
{
    if(id != current.id) {
        return;
    }
    auto newData = model->getModpackById(id);
    if(newData) {
        current = *newData;
        updateCurrentPackUI();
    }
}

static QString versionToString(const CurseForge::ModVersion& version) {
    switch(version.type) {
        case CurseForge::VersionType::Alpha: {
            return QString("%1 (Alfa)").arg(version.displayName);
        }
        case CurseForge::VersionType::Beta: {
            return QString("%1 (Beta)").arg(version.displayName);
        }
        case CurseForge::VersionType::Release: {
            return version.displayName;
        }
        case CurseForge::VersionType::Unknown: {
            break;
        }
    }
    return QString("%1 (?)").arg(version.displayName);
}

void CurseForgePage::updateCurrentPackUI()
{
    switch(current.detailsLoaded) {
        case CurseForge::LoadState::Errored: {
            ui->packDescription->setText(tr("Falha ao buscar detalhes do modpack do CurseForge..."));
            break;
        }
        case CurseForge::LoadState::NotLoaded: {
            ui->packDescription->setText(tr("Carregando..."));
            break;
        }
        case CurseForge::LoadState::Loaded: {
            // CurseForge body is HTML, set it directly
            ui->packDescription->setHtml(current.body);
            break;
        }
    }
    if(current.versions.size() == 0) {
        ui->versionSelectionBox->clear();
    }
    else {
        ui->versionSelectionBox->clear();
        int releaseFound = -1;
        int i = 0;
        for(auto & version: current.versions) {
            ui->versionSelectionBox->addItem(versionToString(version), QVariant::fromValue(version));
            if(releaseFound == -1 && version.type == CurseForge::VersionType::Release) {
                releaseFound = i;
            }
            i++;
        }
        if(releaseFound != -1) {
            ui->versionSelectionBox->setCurrentIndex(releaseFound);
        }
        else if(current.versions.size() != 0) {
            ui->versionSelectionBox->setCurrentIndex(0);
        }
    }
    suggestCurrent();
}

void CurseForgePage::forceDocumentLayout()
{
    ui->packDescription->document()->adjustSize();
}
