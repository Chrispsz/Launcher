#include "LauncherLoginStep.h"

#include <QNetworkRequest>

#include "minecraft/auth/AuthRequest.h"
#include "minecraft/auth/Parsers.h"
#include "minecraft/auth/AccountTask.h"

LauncherLoginStep::LauncherLoginStep(AccountData* data) : AuthStep(data) {

}

LauncherLoginStep::~LauncherLoginStep() noexcept = default;

QString LauncherLoginStep::describe() {
    return tr("Acessando serviços Mojang.");
}

void LauncherLoginStep::perform() {
    auto requestURL = "https://api.minecraftservices.com/launcher/login";
    auto uhs = m_data->mojangservicesToken.extra["uhs"].toString();
    auto xToken = m_data->mojangservicesToken.token;

    QString mc_auth_template = R"XXX(
{
    "xtoken": "XBL3.0 x=%1;%2",
    "platform": "PC_LAUNCHER"
}
)XXX";
    auto requestBody = mc_auth_template.arg(uhs, xToken);

    QNetworkRequest request = QNetworkRequest(QUrl(requestURL));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    request.setRawHeader("Accept", "application/json");
    AuthRequest *requestor = new AuthRequest(this);
    connect(requestor, &AuthRequest::finished, this, &LauncherLoginStep::onRequestDone);
    requestor->post(request, requestBody.toUtf8());
    qDebug() << "Getting Minecraft access token...";
}

void LauncherLoginStep::rehydrate() {
    // TODO: check the token validity
}

void LauncherLoginStep::onRequestDone(
    QNetworkReply::NetworkError error,
    QByteArray data,
    QList<QNetworkReply::RawHeaderPair> headers
) {
    auto requestor = qobject_cast<AuthRequest *>(QObject::sender());
    requestor->deleteLater();

    if (error != QNetworkReply::NoError) {
        qWarning() << "Reply error:" << error;
#ifndef NDEBUG
        qDebug() << data;
#endif
        emit finished(
            AccountTaskState::STATE_FAILED_SOFT,
            tr("Falha ao obter o token de acesso Minecraft: %1").arg(requestor->errorString_)
        );
        return;
    }

    if(!Parsers::parseMojangResponse(data, m_data->yggdrasilToken)) {
        qWarning() << "Could not parse login_with_xbox response...";
#ifndef NDEBUG
        qDebug() << data;
#endif
        emit finished(
            AccountTaskState::STATE_FAILED_SOFT,
            tr("Falha ao analisar a resposta do token de acesso Minecraft.")
        );
        return;
    }
    emit finished(AccountTaskState::STATE_WORKING, tr(""));
}
