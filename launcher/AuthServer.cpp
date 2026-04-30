#include "AuthServer.h"

#include <QThread>
#include <QDebug>
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>

struct Request
{
    QString method;
    QString url;
    QMap<QString, QString> headers;
    QByteArray body;

    QJsonDocument json() {
        return QJsonDocument::fromJson(body.data());
    }
};

struct Response
{
    int statusCode;
    QMap<QString, QString> headers;
    QString body;
};

enum ConnectionState
{
    CREATING,
    READING_HEAD,
    READING_BODY,
    PROCESS_REQUEST
};

struct Connection
{
    ConnectionState state;
    Request *request;
    Response *response;
    int leftToRead;
    QByteArray buffer;
};

AuthServer::AuthServer(QObject *parent) : QObject(parent)
{
    m_tcpServer.reset(new QTcpServer(this));

    connect(m_tcpServer.get(), &QTcpServer::newConnection, this, &AuthServer::newConnection);

    if (!m_tcpServer->listen(QHostAddress::LocalHost))
    {
        qCritical() << "Falha ao iniciar servidor de autenticação";
    }
}

quint16 AuthServer::port()
{
    return m_tcpServer->serverPort();
}

void AuthServer::setProfileInfo(const QString &profileId, const QString &profileName)
{
    QMutexLocker locker(&m_profileMutex);
    m_profileId = profileId;
    m_profileName = profileName;
    qDebug() << "AuthServer: perfil definido — id:" << profileId << "nome:" << profileName;
}

void processRequest(AuthServer *authServer, Request *request, Response *response)
{
    qDebug() << "AuthServer: processando requisição:" << request->url;

    // Root — health check / API info
    if (request->url == "/")
    {
        response->body = "{\"Status\":\"OK\",\"Runtime-Mode\":\"productionMode\",\"Application-Author\":\"Chrispsz Launcher\",\"Application-Description\":\"Chrispsz Launcher Auth API.\",\"Specification-Version\":\"1.0.0\",\"Application-Name\":\"chrispsz.launcher.auth\",\"Implementation-Version\":\"1.0.0\",\"Application-Owner\":\"Chrispsz\"}";
        response->statusCode = 200;
        response->headers["Content-Type"] = "application/json; charset=utf-8";
        return;
    }

    // Session join/hasJoined — accept without verification (offline mode)
    if (request->url == "/sessionserver/session/minecraft/join" || request->url == "/sessionserver/session/minecraft/hasJoined")
    {
        response->statusCode = 204;
        return;
    }

    // Profile lookup — /sessionserver/session/minecraft/profile/<uuid>
    // Minecraft calls this to get the player's name and skin.
    // authlib-injector forwards this to our local AuthServer.
    if (request->url.startsWith("/sessionserver/session/minecraft/profile/"))
    {
        // Extract UUID from URL (strip query params like ?unsigned=false)
        QString path = request->url.split("?").at(0);
        QString uuid = path.mid(QString("/sessionserver/session/minecraft/profile/").length());

        // Remove dashes from UUID if present (Minecraft uses both formats)
        uuid.remove('-');

        // Use the profile info stored by LaunchController before launch
        QString profileId = authServer->profileId().isEmpty() ? uuid : authServer->profileId();
        QString profileName = authServer->profileName().isEmpty() ? "Player" : authServer->profileName();

        // Build the profile response matching Mojang's session server format
        // {"id":"<uuid>","name":"<name>","properties":[{"name":"textures","value":"<base64>"}]}
        QString texturesJson = QString("{\"timestamp\":%1,\"profileId\":\"%2\",\"profileName\":\"%3\",\"textures\":{}}")
            .arg(QString::number(QDateTime::currentMSecsSinceEpoch()), profileId, profileName);
        QByteArray texturesBase64 = texturesJson.toUtf8().toBase64();

        response->body = QString("{\"id\":\"%1\",\"name\":\"%2\",\"properties\":[{\"name\":\"textures\",\"value\":\"%3\"}]}")
            .arg(profileId, profileName, QString::fromLatin1(texturesBase64));
        response->statusCode = 200;
        response->headers["Content-Type"] = "application/json; charset=utf-8";
        return;
    }

    // Authenticate/refresh — for local accounts, echo back the data
    if (request->url == "/auth/authenticate" || request->url == "/auth/refresh")
    {
        auto json = request->json().object();
        QString clientToken = json.value("clientToken").toString();
        QString username = json.value(request->url == "/auth/authenticate" ? "username" : "accessToken").toString();

        QString profile = QString("{\"id\":\"%1\",\"name\":\"%2\"}").arg(clientToken, username);

        response->statusCode = 200;
        response->body = QString("{\"accessToken\":\"%1\",\"clientToken\":\"%2\",\"availableProfiles\":[%3],\"selectedProfile\":%3}")
            .arg(username, clientToken, profile);
        response->headers["Content-Type"] = "application/json; charset=utf-8";
        return;
    }

    response->body = "{\"error\":\"Not Found\",\"errorMessage\":\"Endpoint not implemented\"}";
    response->statusCode = 404;
    response->headers["Content-Type"] = "application/json; charset=utf-8";
}

void AuthServer::newConnection()
{
    QTcpSocket *tcpSocket = m_tcpServer->nextPendingConnection();
    Connection *connection = new Connection();

    connect(tcpSocket, &QTcpSocket::readyRead, this, [this, tcpSocket, connection]()
            {
                QByteArray curBuf = tcpSocket->readAll().data();
                qDebug() << "AuthServer: leu" << curBuf.size() << "bytes";

                if (connection->state == CREATING)
                {
                    connection->response = new Response();
                    connection->request = new Request();
                    connection->buffer = QByteArray();
                    connection->state = READING_HEAD;
                }

                if (connection->state == READING_HEAD)
                {
                    connection->buffer.append(curBuf);
                    if (connection->buffer.contains("\r\n\r\n"))
                    {
                        QByteArray head = connection->buffer.left(connection->buffer.indexOf("\r\n\r\n"));
                        QString headStr = head.data();
                        QStringList headList = headStr.split("\r\n");
                        QStringList firstLine = headList.at(0).split(" ");
                        connection->request->method = firstLine.at(0);
                        connection->request->url = firstLine.at(1);

                        for (int i = 1; i < headList.size(); i++)
                        {
                            QStringList header = headList.at(i).split(":");
                            if (header.size() >= 2) {
                                connection->request->headers.insert(header.at(0).trimmed(), header.at(1).trimmed());
                            }
                        }

                        if (connection->request->headers.contains("Content-Length"))
                        {
                            connection->leftToRead = connection->request->headers["Content-Length"].toInt();
                        }
                        else
                        {
                            connection->leftToRead = 0;
                        }

                        curBuf = connection->buffer.mid(connection->buffer.indexOf("\r\n\r\n") + 4);
                        connection->state = READING_BODY;
                    }
                }

                if (connection->state == READING_BODY)
                {
                    connection->request->body.append(curBuf);
                    if (connection->request->body.size() >= connection->leftToRead)
                    {
                        connection->state = PROCESS_REQUEST;
                    }
                }

                if(connection->state == PROCESS_REQUEST){
                    processRequest(this, connection->request, connection->response);

                    if(connection->response->body.size() > 0){
                        connection->response->headers["Content-Length"] = QString::number(connection->response->body.toUtf8().size());
                    }
                    connection->response->headers["Connection"] = "close";

                    QString responseStatusText = "Internal Server Error";
                    if (connection->response->statusCode == 200)
                        responseStatusText = "OK";
                    else if (connection->response->statusCode == 204)
                        responseStatusText = "No Content";
                    else if (connection->response->statusCode == 404)
                        responseStatusText = "Not Found";

                    QString responseHead = QString("HTTP/1.1 %1 %2\r\n").arg(connection->response->statusCode).arg(responseStatusText);
                    for (auto h: connection->response->headers.keys())
                    {
                        responseHead += QString("%1: %2\r\n").arg(h, connection->response->headers[h]);
                    }
                    responseHead += "\r\n";
                    tcpSocket->write(responseHead.toUtf8());
                    tcpSocket->write(connection->response->body.toUtf8());
                    tcpSocket->disconnectFromHost();
                    connection->state = CREATING;
                }
            });
    connect(tcpSocket, &QTcpSocket::disconnected, this, [tcpSocket]()
            { tcpSocket->close(); });
}
