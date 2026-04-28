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

#include "AboutDialog.h"
#include "ui_AboutDialog.h"
#include <QIcon>
#include "Application.h"
#include "BuildConfig.h"

#include <net/NetJob.h>

#include "HoeDown.h"

namespace {
// Credits
// This is a hack, but I can't think of a better way to do this easily without screwing with QTextDocument...
QString getCreditsHtml(QStringList patrons)
{
    QString output;
    QTextStream stream(&output);
    stream.setCodec(QTextCodec::codecForName("UTF-8"));
    stream << "<center>\n";

    stream << "<h3>" << QObject::tr("Autor original", "About Credits") << "</h3>\n";
    stream << "<p>Andrew Okin &lt;<a href='mailto:forkk@forkk.net'>forkk@forkk.net</a>&gt;</p>\n";

    stream << "<h3>" << QObject::tr("Mantenedor", "About Credits") << "</h3>\n";
    stream << "<p>Petr Mr&aacute;zek &lt;<a href='mailto:peterix@gmail.com'>peterix@gmail.com</a>&gt;</p>\n";

    // TODO: grab contributors from git history
    /*
    if(!contributors.isEmpty()) {
        stream << "<h3>" << QObject::tr("Contributors", "About Credits") << "</h3>\n";
        for (auto &contributor : contributors)
        {
            stream << "<p>" << contributor << "</p>\n";
        }
    }
    */

    if(!patrons.isEmpty()) {
        stream << "<h3>" << QObject::tr("Patrocinadores", "About Credits") << "</h3>\n";
        for (QString patron : patrons)
        {
            stream << "<p>" << patron << "</p>\n";
        }
    }

    stream << "</center>\n";
    return output;
}

QString getLicenseHtml()
{
    HoeDown hoedown;
    QFile dataFile(":/documents/COPYING.md");
    dataFile.open(QIODevice::ReadOnly);
    QString output = hoedown.process(dataFile.readAll());
    return output;
}

}

AboutDialog::AboutDialog(QWidget *parent) : QDialog(parent), ui(new Ui::AboutDialog)
{
    ui->setupUi(this);

    QString launcherName = BuildConfig.LAUNCHER_NAME;

    setWindowTitle(tr("About %1").arg(launcherName));

    QString chtml = getCreditsHtml(QStringList());
    ui->creditsText->setHtml(chtml);

    QString lhtml = getLicenseHtml();
    ui->licenseText->setHtml(lhtml);

    ui->urlLabel->setOpenExternalLinks(true);

    ui->icon->setPixmap(APPLICATION->getThemedIcon("logo").pixmap(64));
    ui->title->setText(launcherName);

    ui->versionLabel->setText(tr("Versão") +": " + BuildConfig.printableVersionString());
    ui->platformLabel->setText(tr("Plataforma") +": " + BuildConfig.BUILD_PLATFORM);

    if (BuildConfig.VERSION_BUILD >= 0)
        ui->buildNumLabel->setText(tr("Número do build") +": " + QString::number(BuildConfig.VERSION_BUILD));
    else
        ui->buildNumLabel->setVisible(false);

    if (!BuildConfig.VERSION_CHANNEL.isEmpty())
        ui->channelLabel->setText(tr("Canal") +": " + BuildConfig.VERSION_CHANNEL);
    else
        ui->channelLabel->setVisible(false);

    ui->redistributionText->setHtml(tr(
"<p>Mantemos o %1 como código aberto porque achamos importante poder ver o código-fonte de um projeto como este, e fazemos isso usando a licença Apache.</p>\n"
"<p>Parte do motivo para usar a licença Apache é que não queremos que as pessoas usem o nome &quot;%1&quot; ao redistribuir o projeto. "
"Isso significa que as pessoas devem dedicar tempo para analisar o código-fonte e remover todas as referências a &quot;%1&quot;, incluindo, mas não se limitando ao ícone "
"do projeto e ao título das janelas, (sem <b>%1-fork</b> no título).</p>\n"
"<p>A licença Apache cobre o uso razoável do nome - uma menção às origens do projeto no diálogo Sobre e na licença é aceitável. "
"No entanto, deve estar perfeitamente claro que o projeto é um fork <b>sem</b> implica que você tem nossa bênção.</p>"
    ).arg(launcherName));

    QString urlText("<html><head/><body><p><a href=\"%1\">%1</a></p></body></html>");
    ui->urlLabel->setText(urlText.arg(BuildConfig.LAUNCHER_GIT));

    QString copyText("© 2012-2021 %1");
    ui->copyLabel->setText(copyText.arg(BuildConfig.LAUNCHER_COPYRIGHT));

    connect(ui->closeButton, SIGNAL(clicked()), SLOT(close()));

    connect(ui->aboutQt, &QPushButton::clicked, &QApplication::aboutQt);

    loadPatronList();
}

AboutDialog::~AboutDialog()
{
    delete ui;
}

void AboutDialog::loadPatronList()
{
    netJob = new NetJob("Patreon Patron List", APPLICATION->network());
    netJob->addNetAction(Net::Download::makeByteArray(QUrl("https://files.multimc.org/patrons.txt"), &dataSink));
    connect(netJob.get(), &NetJob::succeeded, this, &AboutDialog::patronListLoaded);
    netJob->start();
}

void AboutDialog::patronListLoaded()
{
    QString patronListStr(dataSink);
    dataSink.clear();
    QString html = getCreditsHtml(patronListStr.split("\n", QString::SkipEmptyParts));
    ui->creditsText->setHtml(html);
}

