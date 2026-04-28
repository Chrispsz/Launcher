#pragma once

#include "Sink.h"

namespace Net {
/*
 * Sink object for downloads that uses an external QByteArray it doesn't own as a target.
 */
class ByteArraySink : public Sink
{
public:
    static const qint64 MAX_SIZE = 100 * 1024 * 1024; // 100MB

    ByteArraySink(QByteArray *output)
        :m_output(output)
    {
        // nil
    };

    virtual ~ByteArraySink()
    {
        // nil
    }

public:
    JobStatus init(QNetworkRequest & request) override
    {
        m_output->clear();
        if(initAllValidators(request))
            return Job_InProgress;
        return Job_Failed;
    };

    JobStatus write(QByteArray & data) override
    {
        if(m_output->size() + data.size() > MAX_SIZE) {
            qCritical() << "ByteArraySink: response too large, aborting";
            return Job_Failed;
        }
        m_output->append(data);
        if(writeAllValidators(data))
            return Job_InProgress;
        return Job_Failed;
    }

    JobStatus abort() override
    {
        m_output->clear();
        failAllValidators();
        return Job_Failed;
    }

    JobStatus finalize(QNetworkReply &reply) override
    {
        if(finalizeAllValidators(reply))
            return Job_Finished;
        return Job_Failed;
    }

    bool hasLocalData() override
    {
        return false;
    }

private:
    QByteArray * m_output;
};
}
