#pragma once

#include "Application.h"
#include "ui/pages/BasePage.h"
#include "CurseForgeData.h"

#include <QWidget>

namespace Ui {
    class CurseForgePage;
}

class NewInstanceDialog;

namespace CurseForge {
    class ListModel;
}

class CurseForgePage : public QWidget, public BasePage
{
    Q_OBJECT

public:
    explicit CurseForgePage(NewInstanceDialog *dialog, QWidget *parent = nullptr);
    ~CurseForgePage() override;

    QString displayName() const override { return tr("CurseForge"); }
    QIcon icon() const override { return APPLICATION->getThemedIcon("flame"); }
    QString id() const override { return "curseforge"; }

    void openedImpl() override;

    bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
    void triggerSearch();
    void onSelectionChanged(QModelIndex first, QModelIndex second);
    void onVersionSelectionChanged(const QString &version);
    void onPackDataChanged(int id);
    void forceDocumentLayout();

private:
    void updateCurrentPackUI();
    void suggestCurrent();

    Ui::CurseForgePage *ui;
    NewInstanceDialog *dialog;
    CurseForge::ListModel *model = nullptr;
    CurseForge::Modpack current;
    CurseForge::ModVersion currentVersion;
};
