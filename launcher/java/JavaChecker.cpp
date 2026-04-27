#include "JavaChecker.h"

#include <QFile>
#include <QProcess>
#include <QMap>
#include <QDebug>
#include <QThread>

#include "JavaUtils.h"
#include "FileSystem.h"
#include "Commandline.h"
#include "Application.h"

JavaChecker::JavaChecker(QObject *parent) : QObject(parent)
{
}

JavaChecker::~JavaChecker()
{
    killProcess();
}

void JavaChecker::killProcess()
{
    killTimer.stop();
    if (m_process)
    {
        // Disconnect all signals to prevent re-entrant calls
        m_process->disconnect(this);

        if (m_process->state() != QProcess::NotRunning)
        {
            m_process->kill();
            m_process->waitForFinished(2000);
        }

        // Delete IMMEDIATELY — not deleteLater().
        // This is critical: shared_qobject_ptr/QObject parenting would use
        // deleteLater() which needs the event loop. Without the event loop
        // (e.g. during destruction), the process would be destroyed later
        // while still running, causing the warning.
        delete m_process;
        m_process = nullptr;
    }
}

void JavaChecker::performCheck()
{
    QString checkerJar = FS::PathCombine(APPLICATION->getJarsPath(), "JavaCheck.jar");

    QStringList args;

    // Clean up any previous process
    killProcess();

    m_process = new QProcess(this);
    if(m_args.size())
    {
        auto extraArgs = Commandline::splitArgs(m_args);
        args.append(extraArgs);
    }
    if(m_minMem != 0)
    {
        args << QString("-Xms%1m").arg(m_minMem);
    }
    if(m_maxMem != 0)
    {
        args << QString("-Xmx%1m").arg(m_maxMem);
    }
    if(m_permGen != 64)
    {
        args << QString("-XX:PermSize=%1m").arg(m_permGen);
    }

    args.append({"-jar", checkerJar});
    m_process->setArguments(args);
    m_process->setProgram(m_path);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    m_process->setProcessEnvironment(CleanEnviroment());
    qDebug() << "Running java checker: " + m_path + args.join(" ");

    connect(m_process, SIGNAL(finished(int, QProcess::ExitStatus)), this, SLOT(finished(int, QProcess::ExitStatus)));
    connect(m_process, SIGNAL(error(QProcess::ProcessError)), this, SLOT(error(QProcess::ProcessError)));
    connect(m_process, SIGNAL(readyReadStandardOutput()), this, SLOT(stdoutReady()));
    connect(m_process, SIGNAL(readyReadStandardError()), this, SLOT(stderrReady()));
    connect(&killTimer, SIGNAL(timeout()), SLOT(timeout()));
    killTimer.setSingleShot(true);
    killTimer.start(15000);
    m_process->start();
}

void JavaChecker::stdoutReady()
{
    if (!m_process) return;
    QByteArray data = m_process->readAllStandardOutput();
    QString added = QString::fromLocal8Bit(data);
    added.remove('\r');
    m_stdout += added;
}

void JavaChecker::stderrReady()
{
    if (!m_process) return;
    QByteArray data = m_process->readAllStandardError();
    QString added = QString::fromLocal8Bit(data);
    added.remove('\r');
    m_stderr += added;
}

void JavaChecker::finished(int exitcode, QProcess::ExitStatus status)
{
    killTimer.stop();

    if (!m_process) return;

    JavaCheckResult result;
    result.path = m_path;
    result.id = m_id;
    result.errorLog = m_stderr;
    result.outLog = m_stdout;
    qDebug() << "STDOUT" << m_stdout;
    qDebug() << "Java checker finished with status " << status << " exit code " << exitcode;

    if (status == QProcess::CrashExit || exitcode == 1)
    {
        result.validity = JavaCheckResult::Validity::Errored;
        emit checkFinished(result);
        return;
    }

    bool success = true;

    QMap<QString, QString> results;
    QStringList lines = m_stdout.split("\n", QString::SkipEmptyParts);
    for(QString line : lines)
    {
        line = line.trimmed();
        if (line.contains("/bedrock/strata")) {
            continue;
        }

        auto parts = line.split('=', QString::SkipEmptyParts);
        if(parts.size() != 2 || parts[0].isEmpty() || parts[1].isEmpty())
        {
            continue;
        }
        else
        {
            results.insert(parts[0], parts[1]);
        }
    }

    if(!results.contains("os.arch") || !results.contains("java.version") || !results.contains("java.vendor") || !success)
    {
        result.validity = JavaCheckResult::Validity::ReturnedInvalidData;
        emit checkFinished(result);
        return;
    }

    auto os_arch = results["os.arch"];
    auto java_version = results["java.version"];
    auto java_vendor = results["java.vendor"];
    bool is_64 = os_arch == "x86_64" || os_arch == "amd64";

    result.validity = JavaCheckResult::Validity::Valid;
    result.is_64bit = is_64;
    result.mojangPlatform = is_64 ? "64" : "32";
    result.realPlatform = os_arch;
    result.javaVersion = java_version;
    result.javaVendor = java_vendor;
    qDebug() << "Java checker succeeded.";
    emit checkFinished(result);
}

void JavaChecker::error(QProcess::ProcessError err)
{
    if(err == QProcess::FailedToStart)
    {
        qDebug() << "Java checker has failed to start.";
        killTimer.stop();
        JavaCheckResult result;
        result.path = m_path;
        result.id = m_id;

        emit checkFinished(result);
        return;
    }
}

void JavaChecker::timeout()
{
    if(m_process)
    {
        qDebug() << "Java checker killed by timeout.";
        killProcess();
        JavaCheckResult result;
        result.path = m_path;
        result.id = m_id;
        emit checkFinished(result);
    }
}
