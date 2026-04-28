#include "CurseForgePage.h"
#include "ui_CurseForgePage.h"
#include "ui/dialogs/NewInstanceDialog.h"
#include "DesktopServices.h"
#include "InstanceImportTask.h"

#include <QFileInfo>
#include <QFileDialog>

CurseForgePage::CurseForgePage(NewInstanceDialog *dialog, QWidget *parent)
    : QWidget(parent), ui(new Ui::CurseForgePage), dialog(dialog)
{
    ui->setupUi(this);

    // Connect the "Visit CurseForge" button to open the modpack browser
    connect(ui->visitCurseForgeBtn, &QPushButton::clicked, this, []() {
        DesktopServices::openUrl(QUrl("https://www.curseforge.com/minecraft/modpacks"));
    });

    // Connect the "Import from File" button to select a .zip file
    connect(ui->importFileBtn, &QPushButton::clicked, this, [this]() {
        QString filter = tr("Modpack ZIP (*.zip)");
        QString file = QFileDialog::getOpenFileName(this, tr("Selecionar modpack CurseForge"), QString(), filter);
        if (!file.isEmpty()) {
            QFileInfo fi(file);
            auto task = new InstanceImportTask(QUrl::fromLocalFile(file));
            dialog->setSuggestedPack(fi.completeBaseName(), task);
        }
    });
}

CurseForgePage::~CurseForgePage()
{
    delete ui;
}

void CurseForgePage::openedImpl()
{
    BasePage::openedImpl();
}
