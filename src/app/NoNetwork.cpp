// SPDX-License-Identifier: GPL-2.0-only
#include "app/NoNetwork.h"

#include "core/AppLog.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

class RefusedReply : public QNetworkReply
{
public:
    RefusedReply(const QNetworkRequest &request, QNetworkAccessManager::Operation op,
                 QObject *parent)
        : QNetworkReply(parent)
    {
        setRequest(request);
        setUrl(request.url());
        setOperation(op);
        setError(ContentAccessDenied, u"Talos does not load remote content"_s);
        setFinished(true);
        QTimer::singleShot(0, this, [this] {
            emit errorOccurred(ContentAccessDenied);
            emit finished();
        });
    }
    void abort() override {}

protected:
    qint64 readData(char *, qint64) override { return -1; }
};

class LocalOnlyManager : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

protected:
    QNetworkReply *createRequest(Operation op, const QNetworkRequest &request,
                                 QIODevice *outgoing) override
    {
        const QString scheme = request.url().scheme().toLower();
        if (scheme == "qrc"_L1 || scheme == "file"_L1 || scheme == "data"_L1
            || scheme == "assets"_L1 || scheme == "content"_L1)
            return QNetworkAccessManager::createRequest(op, request, outgoing);
        AppLog::debug("ui", u"refused to load remote content from %1"_s.arg(request.url().host()));
        return new RefusedReply(request, op, this);
    }
};

} // namespace

QNetworkAccessManager *NoNetworkFactory::create(QObject *parent)
{
    return new LocalOnlyManager(parent);
}

} // namespace talos
