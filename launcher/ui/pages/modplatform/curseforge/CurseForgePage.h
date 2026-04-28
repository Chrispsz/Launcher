#pragma once

#include "Application.h"
#include "ui/pages/BasePage.h"

#include <QWidget>

namespace Ui {
    class CurseForgePage;
}

class NewInstanceDialog;

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

private:
    Ui::CurseForgePage *ui;
    NewInstanceDialog *dialog;
};
