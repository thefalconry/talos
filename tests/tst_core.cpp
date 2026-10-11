// SPDX-License-Identifier: GPL-2.0-only
// Unit and integration tests for the client core.
//
// The integration half drives a real Session against tools/mock-bridge.mjs
// over real sockets (plain HTTP, self-signed TLS and mutual TLS), so it
// needs `node` and, for the TLS cases, `openssl` on PATH. Without them
// those cases are skipped, not failed.

#include "core/BridgeClient.h"
#include "core/BridgeLinks.h"
#include "core/Crypto.h"
#include "core/Markdown.h"
#include "core/Models.h"
#include "core/Profile.h"
#include "core/Session.h"
#include "core/SseParser.h"
#include "core/Store.h"
#include "mesh/DeviceExec.h"
#include "mesh/MeshService.h"
#include "mesh/ScreenControl.h"
#include "mesh/SelfUpdate.h"
#include "mesh/ServiceInstall.h"

#include <QCryptographicHash>
#include <QJsonDocument>
#include <QProcess>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

using namespace talos;
using namespace Qt::StringLiterals;

namespace {

QJsonObject fixture(const char *name)
{
    QFile f(QStringLiteral(TALOS_TEST_DATA "/") + QLatin1StringView(name));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

/// A mock bridge process plus a side channel to poke and inspect it.
class Mock
{
public:
    ~Mock() { stop(); }

    bool start(const QStringList &extra, const QString &dir)
    {
        const QString node = QStandardPaths::findExecutable(u"node"_s);
        if (node.isEmpty())
            return false;
        QStringList args{QStringLiteral(TALOS_TEST_DATA "/../../tools/mock-bridge.mjs"),
                         u"--slow"_s, u"3"_s, u"--dir"_s, dir};
        if (!extra.contains(u"--port"_s))
            args << u"--port"_s << u"0"_s;
        args += extra;
        m_proc.setProgram(node);
        m_proc.setArguments(args);
        m_proc.setProcessChannelMode(QProcess::ForwardedErrorChannel);
        m_proc.start();
        if (!m_proc.waitForStarted(5000) || !m_proc.waitForReadyRead(15'000))
            return false;
        info = QJsonDocument::fromJson(m_proc.readLine()).object();
        return info["port"_L1].toInt() > 0;
    }

    void stop()
    {
        if (m_proc.state() == QProcess::NotRunning)
            return;
        m_proc.kill();
        m_proc.waitForFinished(3000);
    }

    Profile profile() const
    {
        Profile p = Profile::defaults();
        p.local = false;
        p.host = u"127.0.0.1"_s;
        p.port = info["port"_L1].toInt();
        p.tls = info["scheme"_L1].toString() == "https"_L1;
        p.token = info["token"_L1].toString();
        p.name = u"mock"_s;
        return p;
    }

    QByteArray p12() const
    {
        const QString path = info["p12"_L1].toString();
        if (path.isEmpty())
            return {};
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

    /// Talk to the mock's test hooks, trusting its certificate outright.
    QJsonObject call(const QString &path, const QJsonValue &body = {})
    {
        Profile p = profile();
        p.clientP12 = p12();
        p.clientP12Password = info["p12Password"_L1].toString();
        BridgeClient client(p);
        QJsonObject out;
        bool done = false;
        const auto cb = [&](const ApiResult &r) {
            out = r.json;
            out["__ok"_L1] = r.ok();
            done = true;
        };
        if (body.isUndefined() || body.isNull()) {
            client.get(path, {}, cb);
        } else if (body.isArray()) {
            // The client's post() takes an object; arrays go up one by one.
            for (const auto &v : body.toArray()) {
                done = false;
                client.post(path, v.toObject(), cb);
                [&] { QTRY_VERIFY_WITH_TIMEOUT(done, 5000); }();
            }
            return out;
        } else {
            client.post(path, body.toObject(), cb);
        }
        [&] { QTRY_VERIFY_WITH_TIMEOUT(done, 5000); }();
        return out;
    }

    void emitEvent(const QJsonObject &event) { call(u"/__emit"_s, event); }
    QJsonObject state() { return call(u"/__state"_s); }

    QJsonObject info;

private:
    QProcess m_proc;
};

bool haveOpenssl()
{
    return !QStandardPaths::findExecutable(u"openssl"_s).isEmpty();
}

} // namespace

class CoreTest : public QObject
{
    Q_OBJECT

private slots:
    // ── Pure logic ──────────────────────────────────────────────────────────

    void hostInput_data()
    {
        QTest::addColumn<QString>("raw");
        QTest::addColumn<QString>("host");
        QTest::addColumn<int>("port");
        QTest::addColumn<int>("tls");
        QTest::newRow("bare ip") << "192.168.1.20" << "192.168.1.20" << -1 << -1;
        QTest::newRow("host:port") << "talon.lan:19880" << "talon.lan" << 19880 << -1;
        QTest::newRow("https url") << " https://talon.example.com/ " << "talon.example.com" << -1 << 1;
        QTest::newRow("url with path") << "http://10.0.0.5:8080/health?x=1" << "10.0.0.5" << 8080 << 0;
        QTest::newRow("bracketed v6") << "[::1]:19880" << "::1" << 19880 << -1;
        QTest::newRow("bare v6") << "fe80::1" << "fe80::1" << -1 << -1;
        QTest::newRow("userinfo dropped") << "https://evil@good.example:444" << "good.example" << 444 << 1;
        QTest::newRow("bad port kept in host") << "host:99999" << "host:99999" << -1 << -1;
    }

    void hostInput()
    {
        QFETCH(QString, raw);
        const HostInput in = parseHostInput(raw);
        QTEST(in.host, "host");
        QTEST(in.port, "port");
        QTEST(in.tls, "tls");
    }

    void baseUrlOmitsDefaultPort()
    {
        Profile p;
        p.host = u"talon.example.com"_s;
        p.tls = true;
        p.port = 443;
        QCOMPARE(p.baseUrl(), u"https://talon.example.com"_s);
        p.port = 19880;
        QCOMPARE(p.baseUrl(), u"https://talon.example.com:19880"_s);
        p.host = u"::1"_s;
        QCOMPARE(p.baseUrl(), u"https://[::1]:19880"_s);
    }

    void fingerprints()
    {
        const QString hex = QString(64, u'a');
        QCOMPARE(normalizeFingerprint(u"AA:"_s.repeated(32).chopped(1)), hex);
        QCOMPARE(normalizeFingerprint(u"not a fingerprint"_s), QString());
        QCOMPARE(normalizeFingerprint(hex.chopped(1)), QString());
        QVERIFY(formatFingerprint(hex).startsWith(u"AA:AA:"_s));
    }

    void pairLinks()
    {
        const QString fp = QString(64, u'b');
        // https must pin a certificate.
        auto p = profileFromPairLink(u"talon://pair?u=https%3A%2F%2Ftalon.example.com&t=tok&f="_s + fp);
        QVERIFY(p.has_value());
        QCOMPARE(p->host, u"talon.example.com"_s);
        QCOMPARE(p->port, 443);
        QCOMPARE(p->token, u"tok"_s);
        QCOMPARE(p->fingerprint, fp);
        QVERIFY(!p->local);
        QVERIFY(!p->pinObserved); // named by the link, not yet seen on the wire
        QVERIFY(!profileFromPairLink(u"talon://pair?u=https%3A%2F%2Ftalon.example.com&t=tok"_s));

        // Plain http only on a private address.
        QVERIFY(profileFromPairLink(u"talon:pair?u=http://192.168.1.20:19880&t=tok"_s));
        QVERIFY(!profileFromPairLink(u"talon://pair?u=http://example.com:19880&t=tok"_s));
        QVERIFY(!profileFromPairLink(u"talon://pair?u=http://router.lan&t=tok"_s));

        // No scheme, wrong target, look-alike hosts.
        QVERIFY(!profileFromPairLink(u"talon://pair?u=192.168.1.20&t=tok"_s));
        QVERIFY(!profileFromPairLink(u"talon://unpair?u=http://192.168.1.20"_s));
        QVERIFY(!profileFromPairLink(u"https://example.com/pair?u=http://192.168.1.20"_s));
        QVERIFY(!profileFromPairLink(u"talon://pair?u=http://192.168.1.20%2540evil.example&t=x"_s));

        QCOMPARE(findPairLink(u"paste this: talon://pair?u=http://10.0.0.2&t=a now"_s),
                 u"talon://pair?u=http://10.0.0.2&t=a"_s);
        QVERIFY(findPairLink(u"nothing here"_s).isEmpty());
        QVERIFY(isPairPageUrl(u"https://talon.example.com/pair?grant=abc"_s));
        QVERIFY(!isPairPageUrl(u"https://talon.example.com/pair"_s));
    }

    void privateAddresses()
    {
        QVERIFY(isPrivateAddress(u"127.0.0.1"_s));
        QVERIFY(isPrivateAddress(u"10.1.2.3"_s));
        QVERIFY(isPrivateAddress(u"172.20.0.1"_s));
        QVERIFY(isPrivateAddress(u"192.168.0.9"_s));
        QVERIFY(isPrivateAddress(u"fd00::1"_s));
        QVERIFY(!isPrivateAddress(u"172.32.0.1"_s));
        QVERIFY(!isPrivateAddress(u"8.8.8.8"_s));
        QVERIFY(!isPrivateAddress(u"nas.local"_s));
    }

    void credentialsAndRedaction()
    {
        const QJsonObject auth = fixture("auth_v1.json");
        for (const auto &v : auth["sampleCredentials"_L1].toArray())
            QVERIFY(isDeviceCredential(v.toString()));
        for (const auto &v : auth["sampleSharedTokens"_L1].toArray())
            QVERIFY(!isDeviceCredential(v.toString()));

        const QString cred = auth["sampleCredentials"_L1].toArray().first().toString();
        const QString text = u"GET https://h/events?token=hunter2&x=1 Bearer abc.def "_s + cred;
        const QString clean = redactSecrets(text);
        QVERIFY(!clean.contains(u"hunter2"_s));
        QVERIFY(!clean.contains(u"abc.def"_s));
        QVERIFY(!clean.contains(cred));
        QVERIFY(clean.contains(u"x=1"_s));
    }

    void sseParser()
    {
        SseParser p;
        // Split mid-line, mid-UTF-8 and between CR and LF.
        const QByteArray stream =
            ": ping\n\nretry: 3000\n\ndata: {\"a\":1}\n\ndata: one\r\ndata: two\r\n\r\n"
            "event: x\ndata:{\"k\":\"é\"}\n\ndatax: nope\n\n";
        QByteArrayList got;
        for (int i = 0; i < stream.size(); i += 3)
            got += p.feed(stream.mid(i, 3));
        QCOMPARE(got.size(), 3);
        QCOMPARE(got.at(0), QByteArray("{\"a\":1}"));
        QCOMPARE(got.at(1), QByteArray("one\ntwo"));
        QCOMPARE(got.at(2), QByteArray("{\"k\":\"é\"}"));
        // An unterminated event is not delivered.
        QVERIFY(p.feed("data: partial\n").isEmpty());
    }

    void markdownBlocks()
    {
        const QVariantList blocks = markdown::split(
            u"Intro `inline```` not a fence\n\n```cpp\nint x;\n```\ntail\n~~~\nopen"_s);
        QCOMPARE(blocks.size(), 4);
        QCOMPARE(blocks.at(0).toMap().value(u"type"_s).toString(), u"text"_s);
        QCOMPARE(blocks.at(1).toMap().value(u"lang"_s).toString(), u"cpp"_s);
        QCOMPARE(blocks.at(1).toMap().value(u"code"_s).toString(), u"int x;"_s);
        QVERIFY(blocks.at(1).toMap().value(u"closed"_s).toBool());
        QCOMPARE(blocks.at(2).toMap().value(u"text"_s).toString(), u"tail"_s);
        // Mid-stream: a fence still open is a code block, not stray backticks.
        QVERIFY(!blocks.at(3).toMap().value(u"closed"_s).toBool());
        QCOMPARE(blocks.at(3).toMap().value(u"code"_s).toString(), u"open"_s);

        // A longer fence may contain a shorter one.
        const QVariantList nested = markdown::split(u"````md\n```js\nx\n```\n````"_s);
        QCOMPARE(nested.size(), 1);
        QCOMPARE(nested.first().toMap().value(u"code"_s).toString(), u"```js\nx\n```"_s);

        const QString speech = markdown::toSpeech(u"# Title\n**Bold** [link](http://x)\n```\ncode\n```"_s);
        QVERIFY(speech.contains(u"Title"_s) && speech.contains(u"link"_s));
        QVERIFY(!speech.contains(u'#') && !speech.contains(u'*') && !speech.contains(u"http"_s));
    }

    void crypto()
    {
        const QByteArray salt = crypto::randomBytes(crypto::kSaltBytes);
        const QByteArray key = crypto::argon2id("correct horse", salt, 1024, 2);
        QCOMPARE(key.size(), crypto::kKeyBytes);
        QCOMPARE(crypto::argon2id("correct horse", salt, 1024, 2), key);
        QVERIFY(crypto::argon2id("wrong horse", salt, 1024, 2) != key);

        const QByteArray sealed = crypto::seal(key, "snapshot", "profile-1");
        QCOMPARE(crypto::open(key, sealed, "profile-1").value_or({}), QByteArray("snapshot"));
        QVERIFY(!crypto::open(key, sealed, "profile-2"));
        QVERIFY(!crypto::open(crypto::randomBytes(32), sealed, "profile-1"));
        QByteArray tampered = sealed;
        tampered[tampered.size() - 1] = char(tampered.back() ^ 1);
        QVERIFY(!crypto::open(key, tampered, "profile-1"));
        // Two seals of the same text differ: the nonce is random.
        QVERIFY(crypto::seal(key, "snapshot", "profile-1") != sealed);
    }

    void wireFixtures()
    {
        const QJsonObject f = fixture("protocol_v1.json");
        QVERIFY(!f.isEmpty());
        const Message m = Message::fromJson(f["message"_L1].toObject());
        QCOMPARE(m.id, u"1024"_s);
        QCOMPARE(m.role, Message::Assistant);
        QCOMPARE(m.attachments.size(), 2);
        QVERIFY(m.attachments.at(0).image && !m.attachments.at(1).image);
        QCOMPARE(m.attachments.at(1).name, u"logs.zip"_s);
        QCOMPARE(m.tools.size(), 2);
        QCOMPARE(m.tools.at(1).error, u"exit 1"_s);
        QCOMPARE(m.tools.at(0).durationMs, 800);
        QCOMPARE(m.tokensOut, 340);
        QCOMPARE(m.reactions, QStringList{u"👍"_s});

        const Chat c = Chat::fromJson(f["chat"_L1].toObject());
        QCOMPARE(c.title, u"Protocol design"_s);
        QCOMPARE(c.effort, u"high"_s);
        QVERIFY(c.pulse && !c.hasQueued && !c.contextKnown);

        // Tolerance: wrong types and missing fields never throw or misparse.
        const Message odd = Message::fromJson({{"id"_L1, 7}, {"text"_L1, QJsonValue()},
                                               {"tools"_L1, u"nope"_s}, {"future"_L1, true}});
        QCOMPARE(odd.id, u"7"_s);
        QVERIFY(odd.text.isEmpty() && odd.tools.isEmpty());
        QCOMPARE(Chat::fromJson({{"title"_L1, u"a &amp; b &lt;c&gt;"_s}}).title, u"a & b <c>"_s);
    }

    void messageOrdering()
    {
        const auto msg = [](const QString &id, qint64 ts) {
            Message m;
            m.id = id;
            m.ts = ts;
            return m;
        };
        QList<Message> rows{msg(u"sys-9"_s, 5), msg(u"12"_s, 5), msg(u"3"_s, 5), msg(u"99"_s, 1)};
        std::sort(rows.begin(), rows.end(), messageLess);
        QStringList order;
        for (const Message &m : rows)
            order << m.id;
        // By time, then numeric id (3 before 12), local notes last.
        QCOMPARE(order, (QStringList{u"99"_s, u"3"_s, u"12"_s, u"sys-9"_s}));
    }

    void toolDescriptions()
    {
        QVariantMap d = describeTool(u"mcp__search__web_search"_s, {{"query"_L1, u"qt sse"_s}});
        QCOMPARE(d.value(u"label"_s).toString(), u"Web search"_s);
        QCOMPARE(d.value(u"group"_s).toString(), u"search"_s);
        QCOMPARE(d.value(u"detail"_s).toString(), u"qt sse"_s);
        d = describeTool(u"Bash"_s, {{"command"_L1, u"ls   -la\n/tmp"_s}});
        QCOMPARE(d.value(u"detail"_s).toString(), u"ls -la /tmp"_s);
    }

    void chatListOrderAndFilter()
    {
        ChatListModel model;
        ChatFilterModel filter;
        filter.setSourceModel(&model);
        const auto chat = [](const QString &id, const QString &title, qint64 at) {
            Chat c;
            c.id = id;
            c.title = title;
            c.lastActive = at;
            return c;
        };
        model.setChats({chat(u"a"_s, u"Alpha"_s, 10), chat(u"b"_s, u"Beta"_s, 30),
                        chat(u"c"_s, u"Gamma"_s, 20)});
        const auto order = [&] {
            QStringList ids;
            for (const Chat &c : model.chats())
                ids << c.id;
            return ids.join(u',');
        };
        QCOMPARE(order(), u"b,c,a"_s);
        model.upsert(chat(u"a"_s, u"Alpha"_s, 40)); // activity moves it to the top
        QCOMPARE(order(), u"a,b,c"_s);
        model.upsert(chat(u"d"_s, u"Delta"_s, 25)); // new, lands in the middle
        QCOMPARE(order(), u"a,b,d,c"_s);
        model.upsert(chat(u"a"_s, u"Alpha"_s, 5)); // and back to the bottom
        QCOMPARE(order(), u"b,d,c,a"_s);

        filter.setFilter(u"ta"_s);
        QCOMPARE(filter.rowCount(), 2); // Beta, Delta
        filter.setFilter({});
        QCOMPARE(filter.rowCount(), 4);
        QCOMPARE(ChatListModel::sectionFor(QDateTime::currentMSecsSinceEpoch()), u"Today"_s);
    }

    void heartbeatBackoff()
    {
        QCOMPARE(MeshService::heartbeatDelayMs(0), 60'000);
        QCOMPARE(MeshService::heartbeatDelayMs(1), 30'000);
        QCOMPARE(MeshService::heartbeatDelayMs(2), 60'000);
        QCOMPARE(MeshService::heartbeatDelayMs(4), 240'000);
        QCOMPARE(MeshService::heartbeatDelayMs(5), 300'000);
        QCOMPARE(MeshService::heartbeatDelayMs(500), 300'000);
    }

    void meshCapabilitiesMatchProtocol()
    {
        const QJsonObject f = fixture("mesh_v1.json");
        QStringList core, control;
        for (const auto &v : f["companionCoreCapabilities"_L1].toArray())
            core << v.toString();
        for (const auto &v : f["companionDeviceControlCapabilities"_L1].toArray())
            control << v.toString();
        QCOMPARE(MeshService::capabilitiesFor(false), core);
        // install_apk is Android package installation at an elevated tier,
        // which this client does not implement and so must not advertise.
        control.removeAll(u"install_apk"_s);
        QCOMPARE(MeshService::capabilitiesFor(true), core + control);
    }

    void deviceExecFixtureCommands()
    {
        QTemporaryDir tmp;
        const QJsonObject f = fixture("mesh_v1.json");
        const QJsonObject files = f["sandboxFiles"_L1].toObject();
        for (auto it = files.begin(); it != files.end(); ++it) {
            QFile out(tmp.filePath(it.key()));
            QVERIFY(out.open(QIODevice::WriteOnly));
            out.write(it.value().toString().toUtf8());
        }
        DeviceExec exec;
        int ran = 0;
        for (const auto &v : f["commands"_L1].toArray()) {
            const QJsonObject entry = v.toObject();
            if (!entry["run"_L1].toBool())
                continue;
            const QJsonObject cmd = entry["command"_L1].toObject();
            const QString name = cmd["name"_L1].toString();
            if (name == "ring"_L1 || name == "status"_L1)
                continue; // answered by the mesh service, not the executor
            const QByteArray raw = QJsonDocument(cmd["params"_L1].toObject()).toJson();
            const QJsonObject params = QJsonDocument::fromJson(
                QByteArray(raw).replace("{{TMP}}", tmp.path().toUtf8())).object();
            bool answered = false;
            CommandOutcome outcome;
            QVERIFY2(exec.handle(name, params, [&](const CommandOutcome &o) {
                outcome = o;
                answered = true;
            }), qPrintable(name));
            QTRY_VERIFY_WITH_TIMEOUT(answered, 10'000);
            QVERIFY2(outcome.ok == entry["expectOk"_L1].toBool(),
                     qPrintable(name + u": "_s + outcome.message));
            // The daemon's tools read exactly these keys.
            for (const auto &key : entry["dataKeys"_L1].toArray())
                QVERIFY2(outcome.data.contains(key.toString()),
                         qPrintable(name + u" lacks "_s + key.toString()));
            ++ran;
        }
        QCOMPARE(ran, 8);
        QVERIFY(QFile::exists(tmp.filePath(u"write.txt"_s)));
        QVERIFY(!QFile::exists(tmp.filePath(u"delete-me.txt"_s)));
        QVERIFY(QFile::exists(tmp.filePath(u"move-dst.txt"_s)));
        QVERIFY(QDir(tmp.filePath(u"newdir"_s)).exists());
    }

    void deviceExecEdges()
    {
        QTemporaryDir tmp;
        DeviceExec exec;
        // Chunks must be appended in order.
        const QString path = tmp.filePath(u"chunks.bin"_s);
        QVERIFY(exec.writeFile(path, "abc", 0, true).ok);
        QVERIFY(exec.writeFile(path, "def", 3, false).ok);
        QVERIFY(!exec.writeFile(path, "zzz", 3, false).ok);
        const CommandOutcome read = exec.readFile(path, 2, 2);
        QCOMPARE(QByteArray::fromBase64(read.data["base64"_L1].toString().toLatin1()),
                 QByteArray("cd"));
        QVERIFY(!read.data["eof"_L1].toBool());
        QVERIFY(exec.readFile(path, 4, 100).data["eof"_L1].toBool());

        exec.writeLimit = [] { return qint64(4); };
        QVERIFY(!exec.writeFile(tmp.filePath(u"big"_s), "12345", 0, true).ok);

        // A failing command still carries its output and exit code back.
        CommandOutcome o;
        bool done = false;
        exec.exec(u"echo out; echo err 1>&2; exit 3"_s, tmp.path(), 5000,
                  [&](const CommandOutcome &r) { o = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10'000);
        QVERIFY(!o.ok);
        QCOMPARE(o.data["exitCode"_L1].toInt(), 3);
        QCOMPARE(o.data["stdout"_L1].toString().trimmed(), u"out"_s);
        QCOMPARE(o.data["stderr"_L1].toString().trimmed(), u"err"_s);

        done = false;
        exec.exec(u"sleep 30"_s, {}, 1000, [&](const CommandOutcome &r) { o = r; done = true; });
        QTRY_VERIFY_WITH_TIMEOUT(done, 10'000);
        QVERIFY(!o.ok);
        QVERIFY(o.data["stderr"_L1].toString().contains(u"timeout"_s));
    }

    void selfUpdateOnlyAcceptsTalosBuilds()
    {
        QTemporaryDir dir;
        const auto write = [&](const QString &name, const QByteArray &bytes) {
            QFile f(dir.filePath(name));
            [&] { QVERIFY(f.open(QIODevice::WriteOnly)); }();
            f.write(bytes);
            return dir.filePath(name);
        };
        const auto sha = [](const QString &path) {
            QFile f(path);
            if (!f.open(QIODevice::ReadOnly))
                return QString();
            return QString::fromLatin1(
                QCryptographicHash::hash(f.readAll(), QCryptographicHash::Sha256).toHex());
        };
        // The marker is assembled here so this test binary carries no
        // second complete one.
        const QByteArray marker = QByteArray("TALOS-BUILD-") + "VERSION:";
        const QString exe = write(u"talos"_s, "old program " + marker + "1.0.0;");
        const QString newer = write(u"newer"_s, QByteArray(3000, 'x') + marker + "1.2.0; tail");
        const QString older = write(u"older"_s, marker + "0.9.0;");
        const QString node = write(u"talon-node"_s, "a different program entirely");

        QCOMPARE(selfupdate::versionOf(newer), u"1.2.0"_s);
        QCOMPARE(selfupdate::versionOf(node), QString());
        // This test binary links the core, so it is a Talos build itself.
        QCOMPARE(selfupdate::versionOf(QCoreApplication::applicationFilePath()),
                 QStringLiteral(TALOS_VERSION));

        QString error;
        selfupdate::Request req{newer, u"00"_s.repeated(32), u"1.0.0"_s, false};
        QVERIFY(selfupdate::install(exe, req, &error).isEmpty());
        QVERIFY(error.contains(u"integrity"_s));
        QCOMPARE(selfupdate::versionOf(exe), u"1.0.0"_s); // untouched

        req = {node, sha(node), u"1.0.0"_s, false};
        QVERIFY(selfupdate::install(exe, req, &error).isEmpty());
        QVERIFY(error.contains(u"not a Talos build"_s));

        req = {older, sha(older), u"1.0.0"_s, false};
        QVERIFY(selfupdate::install(exe, req, &error).isEmpty());
        QVERIFY(error.contains(u"downgrade"_s));
        QVERIFY(!QFile::exists(dir.filePath(u".talos-update"_s))); // nothing left staged

        req = {newer, sha(newer), u"1.0.0"_s, false};
        QCOMPARE(selfupdate::install(exe, req, &error), u"1.2.0"_s);
        QCOMPARE(selfupdate::versionOf(exe), u"1.2.0"_s);
        QVERIFY(QFileInfo(exe).isExecutable());

        req = {older, sha(older), u"1.2.0"_s, true};
        QCOMPARE(selfupdate::install(exe, req, &error), u"0.9.0"_s);

        QCOMPARE(selfupdate::compareVersions(u"v1.2.0"_s, u"1.10.0"_s), -1);
        QCOMPARE(selfupdate::compareVersions(u"2.0.0-rc1"_s, u"2.0.0"_s), -1);
        QCOMPARE(selfupdate::compareVersions(u"talos-v1.0"_s, u"1.0.0"_s), 0);
    }

    void serviceUnit()
    {
        const QString unit = service::unitText(u"/opt/My Apps/talos 100%"_s);
        // A path with spaces stays one argument, and % is not a specifier.
        QVERIFY(unit.contains(u"ExecStart=\"/opt/My Apps/talos 100%%\" --headless\n"_s));
        QVERIFY(unit.contains(u"Restart=always"_s));
        QVERIFY(unit.contains(u"WantedBy=default.target"_s));
    }

    void screenControlGeometryAndKeys()
    {
        double f = 0;
        QCOMPARE(ScreenControl::spaceFor({2560, 1440}, 1280, &f), QSize(1280, 720));
        QCOMPARE(f, 2.0);
        QCOMPARE(ScreenControl::spaceFor({1024, 768}, 1280, &f), QSize(1024, 768));
        QCOMPARE(f, 1.0);
        QCOMPARE(ScreenControl::spaceFor({1080, 1920}, 0, &f), QSize(720, 1280)); // portrait

        QCOMPARE(ScreenControl::keysymFor(u"Escape"_s), 0xff1b);
        QCOMPARE(ScreenControl::keysymFor(u"f5"_s), 0xffc2);
        QCOMPARE(ScreenControl::keysymFor(u"a"_s), int('a'));
        QCOMPARE(ScreenControl::keysymFor(u"€"_s), 0x010020ac);
        QCOMPARE(ScreenControl::keysymFor(u"nonsense"_s), 0);

        QList<int> mods;
        int key = 0;
        QString error;
        QVERIFY(ScreenControl::parseCombo(u"ctrl+shift+s"_s, &mods, &key, &error));
        QCOMPARE(mods, (QList<int>{0xffe3, 0xffe1}));
        QCOMPARE(key, int('s'));
        mods.clear();
        QVERIFY(ScreenControl::parseCombo(u"cmd++"_s, &mods, &key, &error));
        QCOMPARE(mods, QList<int>{0xffeb});
        QCOMPARE(key, int('+'));
        mods.clear();
        QVERIFY(ScreenControl::parseCombo(u"escape"_s, &mods, &key, &error));
        QVERIFY(mods.isEmpty());
        QVERIFY(!ScreenControl::parseCombo(u"hyper+x"_s, &mods, &key, &error));
        QVERIFY(!ScreenControl::parseCombo(u"  "_s, &mods, &key, &error));

        // A noisy image is squeezed under the byte budget by lowering the
        // quality first and the size only after that.
        QImage noise(1280, 720, QImage::Format_RGB888);
        for (int y = 0; y < noise.height(); ++y) {
            uchar *line = noise.scanLine(y);
            for (int x = 0; x < noise.width() * 3; ++x)
                line[x] = uchar((x * 131 + y * 71 + (x ^ y) * 13) & 0xff);
        }
        QSize size;
        int quality = 0;
        const QByteArray jpeg = ScreenControl::fitJpeg(noise, 70, 64 * 1024, &size, &quality, &error);
        QVERIFY2(!jpeg.isEmpty(), qPrintable(error));
        QVERIFY(jpeg.size() <= 64 * 1024);
        QVERIFY(quality <= 70);
        QVERIFY(size.width() <= 1280 && size.width() >= 320);
    }

    void storeRoundTrip()
    {
        QTemporaryDir tmp;
        QString id;
        {
            Store store(tmp.path());
            QVERIFY(!store.hasProfile());
            Profile p = Profile::defaults();
            p.local = false;
            p.host = u"talon.example.com"_s;
            p.tls = true;
            p.port = 443;
            p.token = u"s3cret-token"_s;
            p.clientP12 = "p12-bytes";
            p.clientP12Password = u"pw"_s;
            store.saveProfile(p, true);
            id = store.activeProfileId();
            store.setPref(u"theme"_s, u"dark"_s);
            store.setLastRead(u"chat"_s, 42);
            store.writeSnapshot(id, "{\"chats\":[]}");
        }
        // The profile file never holds the credentials.
        QFile profiles(tmp.filePath(u"config/profiles.json"_s));
        QVERIFY(profiles.open(QIODevice::ReadOnly));
        const QByteArray raw = profiles.readAll();
        QVERIFY(!raw.contains("s3cret-token") && !raw.contains("p12-bytes"));
        QCOMPARE(QFile::permissions(tmp.filePath(u"config/secrets.json"_s))
                     & (QFile::ReadGroup | QFile::ReadOther), QFile::Permissions());

        Store store(tmp.path());
        QVERIFY(store.hasProfile());
        QCOMPARE(store.activeProfileMeta().token, QString());
        const Profile back = store.activeProfile();
        QCOMPARE(back.token, u"s3cret-token"_s);
        QCOMPARE(back.clientP12, QByteArray("p12-bytes"));
        QCOMPARE(store.prefString(u"theme"_s), u"dark"_s);
        QCOMPARE(store.lastRead(u"chat"_s), 42);
        QCOMPARE(store.readSnapshot(id), QByteArray("{\"chats\":[]}"));

        // With a lock configured the snapshot is sealed, and unreadable
        // without the key.
        const QByteArray key = crypto::randomBytes(32);
        bool unlocked = true;
        store.snapshotMustSeal = [] { return true; };
        store.snapshotKey = [&] { return unlocked ? key : QByteArray(); };
        store.writeSnapshot(id, "{\"secret\":1}");
        QVERIFY(!QFile::exists(tmp.filePath(u"data/snapshot-%1.json"_s.arg(id))));
        QFile sealed(tmp.filePath(u"data/snapshot-%1.sealed"_s.arg(id)));
        QVERIFY(sealed.open(QIODevice::ReadOnly));
        QVERIFY(!sealed.readAll().contains("secret"));
        QCOMPARE(store.readSnapshot(id), QByteArray("{\"secret\":1}"));
        unlocked = false;
        QVERIFY(store.readSnapshot(id).isEmpty());

        store.removeProfile(id);
        QVERIFY(!store.hasProfile());
        bool found = true;
        SecretStore_readCheck(id, &found);
        QVERIFY(!found);
    }

    // ── Against a live mock bridge ──────────────────────────────────────────

    void sessionAgainstBridge()
    {
        QTemporaryDir home, mockDir;
        Mock mock;
        if (!mock.start({u"--token"_s, u"shared-token"_s}, mockDir.path()))
            QSKIP("node is not available");

        Store store(home.path());
        Session session(&store);
        QSignalSpy arrived(&session, &Session::messageArrived);
        session.applyProfile(mock.profile());
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Connected), 10'000);
        QTRY_COMPARE_WITH_TIMEOUT(session.chatCount(), 4, 5000);
        QCOMPARE(session.botName(), u"Talon"_s);
        QVERIFY(session.hasCapability(u"plugins-skills"_s));

        // Two panes: the most recent chat opens by itself and loads.
        QCOMPARE(session.selectedChatId(), u"d_welcome"_s);
        QTRY_COMPARE_WITH_TIMEOUT(session.messages()->count(), 4, 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.commands().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.models().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(session.devices().size() >= 2, 5000);

        // The shared token is traded for this device's own credential, and
        // the profile on disk holds the new one.
        QTRY_VERIFY_WITH_TIMEOUT(isDeviceCredential(store.activeProfile().token), 5000);
        QVERIFY(isDeviceCredential(session.client()->endpoint().token));
        QCOMPARE(mock.state()["credentialUpgrades"_L1].toArray().first().toObject()
                     ["deviceId"_L1].toString(), store.prefString(u"meshDeviceId"_s));

        // A whole turn: reasoning, a tool call, streamed text, the reply.
        const int before = session.messages()->count();
        session.send(u"hello there"_s, {}, {});
        QTRY_VERIFY_WITH_TIMEOUT(session.turn()->active(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.turn()->reasoning().isEmpty(), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.turn()->active(), 15'000);
        QCOMPARE(session.messages()->count(), before + 2);
        const QModelIndex last = session.messages()->index(before + 1);
        QCOMPARE(last.data(MessageListModel::RoleRole).toString(), u"assistant"_s);
        QVERIFY(last.data(MessageListModel::TextRole).toString().contains(u"hello there"_s));
        const QVariantList tools = last.data(MessageListModel::ToolsRole).toList();
        QCOMPARE(tools.size(), 1);
        QVERIFY(tools.first().toMap().value(u"done"_s).toBool());
        QVERIFY(!last.data(MessageListModel::StatsRole).toString().isEmpty());
        // The stream state is gone once the reply has landed.
        QVERIFY(session.turn()->text().isEmpty() && session.turn()->tools().isEmpty());
        QCOMPARE(arrived.size(), 2);
        // A code block in the reply became its own block.
        bool sawCode = false;
        for (const QVariant &b : last.data(MessageListModel::BlocksRole).toList())
            sawCode |= b.toMap().value(u"type"_s).toString() == "code"_L1;
        QVERIFY(sawCode);

        // Edits, reactions and deletions from the daemon.
        const QString replyId = last.data(MessageListModel::IdRole).toString();
        mock.emitEvent({{"kind"_L1, u"message_edited"_s}, {"chatId"_L1, u"d_welcome"_s},
                        {"messageId"_L1, replyId}, {"text"_L1, u"edited"_s}});
        QTRY_COMPARE_WITH_TIMEOUT(session.messages()->index(before + 1)
                                      .data(MessageListModel::TextRole).toString(),
                                  u"edited"_s, 5000);
        mock.emitEvent({{"kind"_L1, u"reaction"_s}, {"chatId"_L1, u"d_welcome"_s},
                        {"messageId"_L1, replyId}, {"emoji"_L1, u"🎉"_s}});
        QTRY_COMPARE_WITH_TIMEOUT(session.messages()->index(before + 1)
                                      .data(MessageListModel::ReactionsRole).toStringList(),
                                  QStringList{u"🎉"_s}, 5000);
        mock.emitEvent({{"kind"_L1, u"message_deleted"_s}, {"chatId"_L1, u"d_welcome"_s},
                        {"messageId"_L1, replyId}});
        QTRY_COMPARE_WITH_TIMEOUT(session.messages()->count(), before + 1, 5000);

        // Forward compatibility: unknown kinds and extra fields are ignored.
        for (const auto &v : fixture("events_v1.json")["forwardCompat"_L1].toArray())
            mock.emitEvent(v.toObject());
        mock.emitEvent({{"kind"_L1, u"error"_s}, {"chatId"_L1, u"d_welcome"_s},
                        {"message"_L1, u"SDK error: transient"_s}});
        QTRY_COMPARE_WITH_TIMEOUT(session.messages()->count(), before + 2, 5000);
        QCOMPARE(session.messages()->index(before + 1).data(MessageListModel::RoleRole).toString(),
                 u"system"_s);
        // The same failure again does not stack a second note.
        mock.emitEvent({{"kind"_L1, u"error"_s}, {"chatId"_L1, u"d_welcome"_s},
                        {"message"_L1, u"SDK error: transient"_s}});
        QTest::qWait(300);
        QCOMPARE(session.messages()->count(), before + 2);
        QCOMPARE(session.conn(), int(Session::Connected));

        // Activity in another chat marks it unread; opening it clears that.
        const auto unread = [&](const QString &id) {
            for (int i = 0; i < session.chatList()->rowCount(); ++i) {
                const QModelIndex idx = session.chatList()->index(i);
                if (idx.data(ChatListModel::IdRole).toString() == id)
                    return idx.data(ChatListModel::UnreadRole).toBool();
            }
            return false;
        };
        QVERIFY(!unread(u"d_trip"_s));
        QJsonObject trip = mock.call(u"/chats"_s)["chats"_L1].toArray().at(1).toObject();
        QCOMPARE(trip["id"_L1].toString(), u"d_trip"_s);
        trip["lastActive"_L1] = QDateTime::currentMSecsSinceEpoch() + 1000;
        mock.emitEvent({{"kind"_L1, u"chat_updated"_s}, {"chat"_L1, trip}});
        QTRY_VERIFY_WITH_TIMEOUT(unread(u"d_trip"_s), 5000);
        session.selectChat(u"d_trip"_s);
        QVERIFY(!unread(u"d_trip"_s));
        QTRY_COMPARE_WITH_TIMEOUT(session.messages()->count(), 2, 5000);

        // New chat, then walking away from it untouched deletes it again.
        session.newChat();
        QTRY_COMPARE_WITH_TIMEOUT(session.chatCount(), 5, 5000);
        const QString fresh = session.selectedChatId();
        QVERIFY(fresh != u"d_trip"_s);
        session.selectChat(u"d_trip"_s);
        QTRY_COMPARE_WITH_TIMEOUT(session.chatCount(), 4, 5000);

        // The daemon restarts: it says so, the stream drops, and the client
        // is back within a moment rather than after a climbing backoff.
        mock.call(u"/control"_s, QJsonObject{{"action"_L1, u"restart"_s}});
        QTRY_VERIFY_WITH_TIMEOUT(session.conn() != int(Session::Connected), 5000);
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Connected), 8000);

        // A cold start paints from the snapshot before any connection.
        session.persist();
        Session offline(&store);
        QCOMPARE(offline.chatCount(), 4);
        QCOMPARE(offline.conn(), int(Session::Idle));
        QVERIFY(offline.messages()->count() > 0);
    }

    void staleSnapshotGivesWayToTheServer()
    {
        QTemporaryDir home, mockDir;
        Mock mock;
        if (!mock.start({}, mockDir.path()))
            QSKIP("node is not available");
        Store store(home.path());
        store.saveProfile(mock.profile(), true);
        // A snapshot from another life of this daemon: rows the server no
        // longer has, with ids higher than anything it will return, and one
        // whose id a future message will reuse.
        const qint64 now = QDateTime::currentMSecsSinceEpoch();
        const auto row = [&](const QString &id, const QString &text) {
            return QJsonObject{{"id"_L1, id}, {"chatId"_L1, u"d_welcome"_s},
                               {"role"_L1, u"assistant"_s}, {"text"_L1, text}, {"ts"_L1, now}};
        };
        store.writeSnapshot(store.activeProfileId(), QJsonDocument(QJsonObject{
            {"chats"_L1, QJsonArray{QJsonObject{{"id"_L1, u"d_welcome"_s},
                                                 {"title"_L1, u"Old title"_s},
                                                 {"lastActive"_L1, now}}}},
            {"messages"_L1, QJsonObject{{"d_welcome"_L1,
                 QJsonArray{row(u"1011"_s, u"ghost one"_s), row(u"9000"_s, u"ghost two"_s)}}}},
        }).toJson());

        Session session(&store);
        QCOMPARE(session.messages()->count(), 2); // painted from the snapshot
        session.start();
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Connected), 10'000);
        QTRY_COMPARE_WITH_TIMEOUT(session.messages()->count(), 4, 5000);
        const auto texts = [&] {
            QStringList out;
            for (int i = 0; i < session.messages()->count(); ++i)
                out << session.messages()->index(i).data(MessageListModel::TextRole).toString();
            return out.join(u'|');
        };
        QVERIFY(!texts().contains(u"ghost"_s));

        // The next real message takes id 1011. It must show, not be mistaken
        // for the ghost that once had that id.
        session.send(u"are you there"_s, {}, {});
        QTRY_VERIFY_WITH_TIMEOUT(texts().contains(u"are you there"_s), 5000);
        QTRY_VERIFY_WITH_TIMEOUT(!session.turn()->active() && session.messages()->count() == 6,
                                 15'000);

        // A reset empties the chat here too, rather than keeping what was
        // on screen.
        session.resetChat(u"d_welcome"_s);
        QTRY_COMPARE_WITH_TIMEOUT(session.messages()->count(), 0, 5000);
        QTest::qWait(300);
        QCOMPARE(session.messages()->count(), 0);
    }

    void wrongTokenStopsRetrying()
    {
        QTemporaryDir home, mockDir;
        Mock mock;
        if (!mock.start({u"--token"_s, u"right"_s}, mockDir.path()))
            QSKIP("node is not available");
        Store store(home.path());
        Session session(&store);
        Profile p = mock.profile();
        p.token = u"wrong"_s;
        session.applyProfile(p);
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Error), 10'000);
        QVERIFY(session.connFatal());
        QVERIFY(session.connError().contains(u"Unauthorized"_s));
        // Fatal means stopped: no reconnect attempt sneaks in.
        QTest::qWait(1500);
        QCOMPARE(session.conn(), int(Session::Error));
    }

    void nothingListening()
    {
        QTemporaryDir home;
        Store store(home.path());
        Session session(&store);
        Profile p = Profile::defaults();
        p.local = false;
        p.host = u"127.0.0.1"_s;
        p.port = 1; // nothing listens here
        session.applyProfile(p);
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Error), 10'000);
        QVERIFY(!session.connFatal()); // this one heals by retrying
        QVERIFY(session.connError().contains(u"No Talon bridge"_s));
    }

    void localDiscovery()
    {
        QTemporaryDir home, mockDir, talonHome;
        Mock mock;
        if (!mock.start({u"--token"_s, u"local-token"_s, u"--discovery"_s,
                         talonHome.filePath(u"native-bridge.json"_s)}, mockDir.path()))
            QSKIP("node is not available");
        qputenv("TALON_HOME", talonHome.path().toUtf8());
        Store store(home.path());
        Session session(&store);
        // Nothing but "this computer": address and token come from the file.
        Profile p = Profile::defaults();
        p.local = true;
        session.applyProfile(p);
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Connected), 10'000);
        QCOMPARE(session.client()->endpoint().port, mock.info["port"_L1].toInt());
        // A same-machine client keeps the shared token by design.
        QTest::qWait(500);
        QVERIFY(mock.state()["credentialUpgrades"_L1].toArray().isEmpty());
        QVERIFY(store.activeProfile().token.isEmpty());
        qunsetenv("TALON_HOME");
    }

    void tlsPinning()
    {
        if (!haveOpenssl())
            QSKIP("openssl is not available");
        QTemporaryDir home, certsA, certsB;
        Mock mock;
        if (!mock.start({u"--tls"_s, u"--token"_s, u"tok"_s}, certsA.path()))
            QSKIP("node is not available");
        const QString fp = mock.info["fingerprint"_L1].toString();
        const int port = mock.info["port"_L1].toInt();

        // The probe reports the certificate without sending the token.
        QString probed;
        bool reached = false, probedDone = false;
        BridgeClient::probeCertificate(mock.profile(), this, [&](const QString &f, bool r) {
            probed = f;
            reached = r;
            probedDone = true;
        });
        QTRY_VERIFY_WITH_TIMEOUT(probedDone, 10'000);
        QVERIFY(reached);
        QCOMPARE(probed, fp);

        Store store(home.path());
        Session session(&store);
        session.applyProfile(mock.profile()); // no pin yet: first use adopts it
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Connected), 10'000);
        QCOMPARE(store.activeProfileMeta().fingerprint, fp);
        QVERIFY(store.activeProfileMeta().pinObserved);
        session.stop();

        // Same address, different certificate: refused, and it stays refused.
        mock.stop();
        Mock impostor;
        QVERIFY(impostor.start({u"--tls"_s, u"--token"_s, u"tok"_s, u"--port"_s,
                                QString::number(port)}, certsB.path()));
        QVERIFY(impostor.info["fingerprint"_L1].toString() != fp);
        session.start();
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Error), 10'000);
        QVERIFY(session.connFatal());
        QVERIFY(session.connError().contains(u"certificate"_s));
        // The impostor never saw the token.
        QVERIFY(impostor.state()["streams"_L1].toArray().isEmpty());
    }

    void certificateBundleFormats()
    {
        if (!haveOpenssl())
            QSKIP("openssl is not available");
        QTemporaryDir dir;
        const auto openssl = [&](const QStringList &args) {
            QProcess p;
            p.setWorkingDirectory(dir.path());
            p.start(u"openssl"_s, args);
            return p.waitForFinished(30'000) && p.exitCode() == 0;
        };
        QVERIFY(openssl({u"req"_s, u"-x509"_s, u"-newkey"_s, u"rsa:2048"_s, u"-nodes"_s,
                         u"-keyout"_s, u"k.pem"_s, u"-out"_s, u"c.pem"_s, u"-days"_s, u"2"_s,
                         u"-subj"_s, u"/CN=talos-test"_s}));
        const QStringList exportArgs{u"pkcs12"_s, u"-export"_s, u"-in"_s, u"c.pem"_s,
                                     u"-inkey"_s, u"k.pem"_s, u"-passout"_s, u"pass:pw"_s};
        const auto bytes = [&](const QString &name) {
            QFile f(dir.filePath(name));
            return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
        };
        QString why;

        // What OpenSSL 3 writes by default: AES-256 with PBKDF2.
        QVERIFY(openssl(exportArgs + QStringList{u"-out"_s, u"modern.p12"_s}));
        QVERIFY(BridgeClient::checkPkcs12(bytes(u"modern.p12"_s), u"pw"_s, &why));

        // What OpenSSL 1 wrote, and what `-legacy` still writes: RC2 and
        // 3DES, which OpenSSL 3 only reads once its legacy provider is on.
        if (!openssl(exportArgs + QStringList{u"-legacy"_s, u"-out"_s, u"legacy.p12"_s}))
            QSKIP("this openssl cannot write a legacy bundle");
        QVERIFY2(BridgeClient::checkPkcs12(bytes(u"legacy.p12"_s), u"pw"_s, &why),
                 qPrintable(why));
        QVERIFY(!BridgeClient::checkPkcs12(bytes(u"legacy.p12"_s), u"wrong"_s, &why));
        QVERIFY(why.contains(u"password"_s));
        // Turning the legacy provider on must not cost the modern one.
        QVERIFY(BridgeClient::checkPkcs12(bytes(u"modern.p12"_s), u"pw"_s, &why));
    }

    void mutualTlsAndBridgeLinks()
    {
        if (!haveOpenssl())
            QSKIP("openssl is not available");
        QTemporaryDir home, certs, downloads;
        Mock mock;
        if (!mock.start({u"--mtls"_s, u"--token"_s, u"tok"_s}, certs.path()))
            QSKIP("node is not available");
        const QByteArray p12 = mock.p12();
        const QString password = mock.info["p12Password"_L1].toString();
        QVERIFY(!p12.isEmpty());

        QString why;
        QVERIFY(BridgeClient::checkPkcs12(p12, password, &why));
        QVERIFY(!BridgeClient::checkPkcs12(p12, u"wrong"_s, &why));
        QVERIFY(!why.isEmpty());

        Store store(home.path());
        Session session(&store);
        BridgeLinks links(&session, &store);

        // Without the certificate the proxy refuses the handshake, and the
        // error says what to do about it. No retry loop.
        Profile p = mock.profile();
        p.fingerprint = mock.info["fingerprint"_L1].toString();
        session.applyProfile(p);
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Error), 10'000);
        QVERIFY2(session.connFatal(), qPrintable(session.connError()));
        QVERIFY(session.connError().contains(u"client certificate"_s));

        p.clientP12 = p12;
        p.clientP12Password = password;
        session.applyProfile(p);
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Connected), 10'000);
        QTRY_COMPARE_WITH_TIMEOUT(session.chatCount(), 4, 5000);

        // Which links belong to the bridge.
        const QString base = session.client()->endpoint().baseUrl();
        QCOMPARE(links.inspect(base + u"/download"_s).value(u"kind"_s).toString(), u"bridge"_s);
        QCOMPARE(links.inspect(base + u"/secret?grant=live-grant"_s).value(u"kind"_s).toString(),
                 u"secret"_s);
        QCOMPARE(links.inspect(u"https://example.com/download"_s).value(u"kind"_s).toString(),
                 u"external"_s);
        QCOMPARE(links.inspect(u"talon://pair?u=http://10.0.0.2&t=a"_s).value(u"kind"_s).toString(),
                 u"pair"_s);
        // Same host, another port, is another server.
        QVERIFY(!links.isBridgeUrl(QUrl(u"https://127.0.0.1:1/download"_s)));

        // A file on the bridge, fetched over the mTLS connection and saved
        // under the name the server gave it.
        links.setDownloadDir(downloads.path());
        const QString expected = downloads.filePath(u"talon-report.txt"_s);
        links.download(base + u"/download"_s, {});
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(expected), 10'000);
        QFile got(expected);
        QVERIFY(got.open(QIODevice::ReadOnly));
        QCOMPARE(got.readAll(), QByteArray("talon mock download\n").repeated(64));
        // A second copy never overwrites the first.
        links.download(base + u"/download"_s, {});
        QTRY_VERIFY_WITH_TIMEOUT(QFile::exists(downloads.filePath(u"talon-report (1).txt"_s)),
                                 10'000);

        // The secret-drop form, answered natively: the value goes straight
        // to the bridge and the grant is spent.
        links.submitSecretDrop(base + u"/secret?grant=live-grant"_s, u"hunter2"_s, {});
        QTRY_COMPARE_WITH_TIMEOUT(mock.state()["secrets"_L1].toArray().size(), 1, 5000);
        const QJsonObject stored = mock.state()["secrets"_L1].toArray().first().toObject();
        QCOMPARE(stored["name"_L1].toString(), u"wifi-password"_s);
        QCOMPARE(stored["sha256"_L1].toString(),
                 QString::fromLatin1(QCryptographicHash::hash("hunter2", QCryptographicHash::Sha256)
                                         .toHex()));
    }

    void meshDevice()
    {
        QTemporaryDir home, mockDir, work;
        Mock mock;
        if (!mock.start({}, mockDir.path()))
            QSKIP("node is not available");
        Store store(home.path());
        Session session(&store);
        MeshService mesh(&store, &session);
        QSignalSpy rings(&mesh, &MeshService::ringRequested);
        store.setPref(u"meshSharing"_s, true);
        session.applyProfile(mock.profile());
        QTRY_COMPARE_WITH_TIMEOUT(session.conn(), int(Session::Connected), 10'000);
        QTRY_VERIFY_WITH_TIMEOUT(mesh.registered(), 5000);
        const QString me = mesh.deviceId();
        QVERIFY(!me.isEmpty());

        const auto lastRegistration = [&] {
            return mock.state()["registrations"_L1].toArray().last().toObject();
        };
        QCOMPARE(lastRegistration()["id"_L1].toString(), me);
        QCOMPARE(lastRegistration()["capabilities"_L1].toArray().size(), 3);

        const auto command = [&](const QString &id, const QString &name, const QJsonObject &params,
                                 const QString &target = {}) {
            mock.emitEvent({{"kind"_L1, u"device_command"_s}, {"id"_L1, id},
                            {"deviceId"_L1, target.isEmpty() ? me : target},
                            {"name"_L1, name}, {"params"_L1, params}});
        };
        const auto result = [&](const QString &id) {
            for (const auto &v : mock.state()["commandResults"_L1].toArray()) {
                if (v.toObject()["commandId"_L1].toString() == id)
                    return v.toObject();
            }
            return QJsonObject();
        };
        const auto wait = [&](const QString &id) {
            [&] { QTRY_VERIFY_WITH_TIMEOUT(!result(id).isEmpty(), 10'000); }();
            return result(id);
        };

        // Core commands work without device control.
        command(u"c-status"_s, u"status"_s, {});
        const QJsonObject status = wait(u"c-status"_s);
        QVERIFY(status["ok"_L1].toBool());
        for (const char *key : {"name", "platform", "appVersion"})
            QVERIFY(status["data"_L1].toObject().contains(QLatin1StringView(key)));
        command(u"c-ring"_s, u"ring"_s, {{"message"_L1, u"where are you"_s}});
        QVERIFY(wait(u"c-ring"_s)["ok"_L1].toBool());
        QCOMPARE(rings.size(), 1);
        QCOMPARE(rings.first().first().toString(), u"where are you"_s);

        // Shell access is refused until the user grants it for this bridge.
        command(u"c-denied"_s, u"exec"_s, {{"cmd"_L1, u"echo no"_s}});
        const QJsonObject denied = wait(u"c-denied"_s);
        QVERIFY(!denied["ok"_L1].toBool());
        QVERIFY(denied["message"_L1].toString().contains(u"disabled"_s));

        // A command addressed to another device is none of ours.
        command(u"c-other"_s, u"status"_s, {}, u"dev_someone_else"_s);

        mesh.setDeviceControl(true);
        // The file commands and transfers, plus update_node where this
        // program can replace itself (a build tree can).
        const int granted = 13 + (mesh.canUpdateSelf() ? 1 : 0);
        QTRY_COMPARE_WITH_TIMEOUT(lastRegistration()["capabilities"_L1].toArray().size(), granted,
                                  5000);
        command(u"c-exec"_s, u"exec"_s, {{"cmd"_L1, u"echo conformance"_s}, {"cwd"_L1, work.path()}});
        const QJsonObject ran = wait(u"c-exec"_s);
        QVERIFY(ran["ok"_L1].toBool());
        QCOMPARE(ran["data"_L1].toObject()["stdout"_L1].toString().trimmed(), u"conformance"_s);
        QCOMPARE(ran["deviceId"_L1].toString(), me);

        // Local approval can refuse, and the refusal reaches the daemon.
        mesh.approver = [](const QString &, const QString &, auto decide) {
            decide(u"Denied on the device."_s);
        };
        command(u"c-refused"_s, u"exec"_s, {{"cmd"_L1, u"touch %1"_s.arg(work.filePath(u"never"_s))}});
        QVERIFY(!wait(u"c-refused"_s)["ok"_L1].toBool());
        QVERIFY(!QFile::exists(work.filePath(u"never"_s)));
        mesh.approver = nullptr;

        command(u"c-unknown"_s, u"install_apk"_s, {{"path"_L1, u"/x.apk"_s}});
        QVERIFY(!wait(u"c-unknown"_s)["ok"_L1].toBool());

        // update_node with what the daemon would pick by default, a
        // talon-node binary: verified, recognised as not ours, refused.
        if (mesh.canUpdateSelf()) {
            const QString node = work.filePath(u"talon-node"_s);
            QFile fake(node);
            QVERIFY(fake.open(QIODevice::WriteOnly));
            fake.write("not this program");
            fake.close();
            command(u"c-update"_s, u"update_node"_s,
                    {{"path"_L1, node},
                     {"sha256"_L1, QString::fromLatin1(QCryptographicHash::hash(
                                       "not this program", QCryptographicHash::Sha256).toHex())}});
            const QJsonObject update = wait(u"c-update"_s);
            QVERIFY(!update["ok"_L1].toBool());
            QVERIFY2(update["message"_L1].toString().contains(u"not a Talos build"_s),
                     qPrintable(update["message"_L1].toString()));
            QCOMPARE(selfupdate::versionOf(QCoreApplication::applicationFilePath()),
                     QStringLiteral(TALOS_VERSION));
        }
        // Screen control is off unless the app supplies it and the user
        // turned it on.
        command(u"c-screen"_s, u"computer"_s, {{"action"_L1, u"screenshot"_s}});
        QVERIFY(!wait(u"c-screen"_s)["ok"_L1].toBool());

        // Streamed transfers, checked by digest both ways.
        const QByteArray body = "conformance\n";
        const QString digest = QString::fromLatin1(
            QCryptographicHash::hash(body, QCryptographicHash::Sha256).toHex());
        mock.emitEvent({{"__transfer"_L1, QJsonObject{{"token"_L1, u"tok_dn"_s},
                                                       {"body"_L1, QString::fromUtf8(body)}}}});
        mock.emitEvent({{"__transfer"_L1, QJsonObject{{"token"_L1, u"tok_up"_s}}}});
        const QString dest = work.filePath(u"sub/downloaded.bin"_s);
        command(u"c-down"_s, u"download_file"_s,
                {{"token"_L1, u"tok_dn"_s}, {"path"_L1, dest}, {"sha256"_L1, digest}});
        const QJsonObject down = wait(u"c-down"_s);
        QVERIFY2(down["ok"_L1].toBool(), qPrintable(down["message"_L1].toString()));
        QCOMPARE(down["data"_L1].toObject()["sha256"_L1].toString(), digest);
        QFile got(dest);
        QVERIFY(got.open(QIODevice::ReadOnly));
        QCOMPARE(got.readAll(), body);
        QVERIFY(!QFile::exists(dest + u".part"_s));

        // A body that doesn't match the promised digest is discarded.
        mock.emitEvent({{"__transfer"_L1, QJsonObject{{"token"_L1, u"tok_bad"_s},
                                                       {"body"_L1, u"tampered"_s}}}});
        const QString bad = work.filePath(u"bad.bin"_s);
        command(u"c-bad"_s, u"download_file"_s,
                {{"token"_L1, u"tok_bad"_s}, {"path"_L1, bad}, {"sha256"_L1, digest}});
        QVERIFY(!wait(u"c-bad"_s)["ok"_L1].toBool());
        QVERIFY(!QFile::exists(bad) && !QFile::exists(bad + u".part"_s));

        command(u"c-up"_s, u"upload_file"_s, {{"token"_L1, u"tok_up"_s}, {"path"_L1, dest}});
        const QJsonObject up = wait(u"c-up"_s);
        QVERIFY2(up["ok"_L1].toBool(), qPrintable(up["message"_L1].toString()));
        QCOMPARE(up["data"_L1].toObject()["sha256"_L1].toString(), digest);
        const QJsonObject received = mock.state()["transfers"_L1].toArray().last().toObject();
        QCOMPARE(received["sha256"_L1].toString(), digest);
        QCOMPARE(received["deviceId"_L1].toString(), me);

        QVERIFY(result(u"c-other"_s).isEmpty());

        // Everything that ran is in the audit trail, newest first, and the
        // trail never holds a transfer token.
        const QVariantList audit = mesh.audit();
        QVERIFY(audit.size() >= 8);
        QCOMPARE(audit.first().toMap().value(u"name"_s).toString(), u"upload_file"_s);
        QFile trail(home.filePath(u"data/mesh-audit.jsonl"_s));
        QVERIFY(trail.open(QIODevice::ReadOnly));
        QVERIFY(!trail.readAll().contains("tok_"));

        // Turning sharing off stops answering.
        store.setPref(u"meshSharing"_s, false);
        command(u"c-off"_s, u"status"_s, {});
        QTest::qWait(500);
        QVERIFY(result(u"c-off"_s).isEmpty());
    }

private:
    static void SecretStore_readCheck(const QString &id, bool *found);
};

#include "core/SecretStore.h"

void CoreTest::SecretStore_readCheck(const QString &id, bool *found)
{
    SecretStore::instance().read(u"profile/"_s + id, found);
}

QTEST_GUILESS_MAIN(CoreTest)
#include "tst_core.moc"
