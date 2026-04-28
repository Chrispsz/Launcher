/* Copyright 2013-2021 MultiMC Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "AccountListPage.h"
#include "ui_AccountListPage.h"

#include <QItemSelectionModel>
#include <QMenu>

#include <QDebug>

#include "net/NetJob.h"

#include "ui/dialogs/ProgressDialog.h"
#include "ui/dialogs/LocalLoginDialog.h"
#include "ui/dialogs/CustomMessageBox.h"

#include "tasks/Task.h"
#include "minecraft/auth/AccountTask.h"
#include "minecraft/services/SkinDelete.h"

#include "Application.h"

AccountListPage::AccountListPage(QWidget *parent)
    : QMainWindow(parent), ui(new Ui::AccountListPage)
{
    ui->setupUi(this);
    ui->listView->setEmptyString(tr(
        "Bem-vindo!\n"
        "Clique no botão \"Adicionar local\" para adicionar sua conta local."
    ));
    ui->listView->setEmptyMode(VersionListView::String);
    ui->listView->setContextMenuPolicy(Qt::CustomContextMenu);

    m_accounts = APPLICATION->accounts();

    ui->listView->setModel(m_accounts.get());
    ui->listView->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    ui->listView->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    ui->listView->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    ui->listView->setSelectionMode(QAbstractItemView::SingleSelection);

    QItemSelectionModel *selectionModel = ui->listView->selectionModel();

    connect(selectionModel, &QItemSelectionModel::selectionChanged, [this](const QItemSelection &sel, const QItemSelection &dsel) {
        updateButtonStates();
    });
    connect(ui->listView, &VersionListView::customContextMenuRequested, this, &AccountListPage::ShowContextMenu);

    connect(m_accounts.get(), &AccountList::listChanged, this, &AccountListPage::listChanged);
    connect(m_accounts.get(), &AccountList::listActivityChanged, this, &AccountListPage::listChanged);
    connect(m_accounts.get(), &AccountList::defaultAccountChanged, this, &AccountListPage::listChanged);

    updateButtonStates();
}

AccountListPage::~AccountListPage()
{
    delete ui;
}

void AccountListPage::ShowContextMenu(const QPoint& pos)
{
    auto menu = ui->toolBar->createContextMenu(this, tr("Menu de contexto"));
    menu->exec(ui->listView->mapToGlobal(pos));
    delete menu;
}

void AccountListPage::changeEvent(QEvent* event)
{
    if (event->type() == QEvent::LanguageChange)
    {
        ui->retranslateUi(this);
    }
    QMainWindow::changeEvent(event);
}

QMenu * AccountListPage::createPopupMenu()
{
    QMenu* filteredMenu = QMainWindow::createPopupMenu();
    filteredMenu->removeAction(ui->toolBar->toggleViewAction() );
    return filteredMenu;
}


void AccountListPage::listChanged()
{
    updateButtonStates();
}

void AccountListPage::on_actionAddLocal_triggered()
{
    MinecraftAccountPtr account = LocalLoginDialog::newAccount(
        this,
        tr("Digite o nome de usuário desejado para adicionar sua conta.")
    );

    if (account)
    {
        m_accounts->addAccount(account);
        if (m_accounts->count() == 1) {
            m_accounts->setDefaultAccount(account);
        }
    }
}

void AccountListPage::on_actionRemove_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0)
    {
        QModelIndex selected = selection.first();
        m_accounts->removeAccount(selected);
    }
}

void AccountListPage::on_actionRefresh_triggered() {
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0) {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        m_accounts->requestRefresh(account->internalId());
    }
}


void AccountListPage::on_actionSetDefault_triggered()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    if (selection.size() > 0)
    {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        m_accounts->setDefaultAccount(account);
    }
}

void AccountListPage::on_actionNoDefault_triggered()
{
    m_accounts->setDefaultAccount(nullptr);
}

void AccountListPage::updateButtonStates()
{
    QModelIndexList selection = ui->listView->selectionModel()->selectedIndexes();
    bool hasSelection = selection.size() > 0;
    bool accountIsReady = false;
    if (hasSelection)
    {
        QModelIndex selected = selection.first();
        MinecraftAccountPtr account = selected.data(AccountList::PointerRole).value<MinecraftAccountPtr>();
        accountIsReady = !account->isActive();
    }
    ui->actionRemove->setEnabled(accountIsReady);
    ui->actionSetDefault->setEnabled(accountIsReady);
    ui->actionUploadSkin->setEnabled(false);
    ui->actionDeleteSkin->setEnabled(false);
    ui->actionRefresh->setEnabled(accountIsReady);

    if(m_accounts->defaultAccount().get() == nullptr) {
        ui->actionNoDefault->setEnabled(false);
        ui->actionNoDefault->setChecked(true);
    }
    else {
        ui->actionNoDefault->setEnabled(true);
        ui->actionNoDefault->setChecked(false);
    }
}

void AccountListPage::on_actionUploadSkin_triggered()
{
    // Skin upload removed — not supported for local accounts
}

void AccountListPage::on_actionDeleteSkin_triggered()
{
    // Skin delete removed — not supported for local accounts
}
