// SPDX-License-Identifier: GPL-2.0-only
#include "core/Models.h"

#include <QCoreApplication>

#include "core/Markdown.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QLocale>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

QString str(const QJsonValue &v, const QString &fallback = {})
{
    if (v.isString())
        return v.toString();
    if (v.isDouble())
        return QString::number(v.toDouble(), 'f', 0);
    return fallback;
}

qint64 num(const QJsonValue &v, qint64 fallback = 0)
{
    if (v.isDouble())
        return qint64(v.toDouble());
    if (v.isString()) {
        bool ok = false;
        const qint64 n = v.toString().toLongLong(&ok);
        return ok ? n : fallback;
    }
    return fallback;
}

} // namespace

QString unescapeHtml(const QString &text)
{
    if (!text.contains(u'&'))
        return text;
    QString out = text;
    out.replace("&lt;"_L1, "<"_L1)
        .replace("&gt;"_L1, ">"_L1)
        .replace("&quot;"_L1, "\""_L1)
        .replace("&#39;"_L1, "'"_L1)
        .replace("&#x27;"_L1, "'"_L1)
        .replace("&nbsp;"_L1, " "_L1)
        .replace("&amp;"_L1, "&"_L1);
    return out;
}

QString formatBytes(qint64 bytes)
{
    if (bytes <= 0)
        return {};
    if (bytes < 1024)
        return u"%1 B"_s.arg(bytes);
    static const char *units[] = {"KB", "MB", "GB", "TB"};
    double value = bytes / 1024.0;
    int unit = 0;
    while (value >= 1024 && unit < 3) {
        value /= 1024;
        ++unit;
    }
    return u"%1 %2"_s.arg(value < 10 ? QString::number(value, 'f', 1)
                                     : QString::number(qRound(value)),
                          QLatin1StringView(units[unit]));
}

QString formatDuration(qint64 ms)
{
    if (ms < 0)
        return {};
    if (ms < 1000)
        return u"%1 ms"_s.arg(ms);
    if (ms < 60'000)
        return u"%1 s"_s.arg(QString::number(ms / 1000.0, 'f', ms < 10'000 ? 1 : 0));
    const qint64 s = ms / 1000;
    if (s < 3600)
        return u"%1m %2s"_s.arg(s / 60).arg(s % 60);
    return u"%1h %2m"_s.arg(s / 3600).arg((s % 3600) / 60);
}

QString formatCount(qint64 n)
{
    if (n < 1000)
        return QString::number(n);
    if (n < 1'000'000)
        return QString::number(n / 1000.0, 'f', n < 10'000 ? 1 : 0) + u'k';
    return QString::number(n / 1'000'000.0, 'f', 1) + u'M';
}

// ── Attachment ──────────────────────────────────────────────────────────────

Attachment Attachment::fromJson(const QJsonObject &j)
{
    Attachment a;
    a.path = str(j["path"_L1]);
    a.name = str(j["name"_L1]);
    a.size = num(j["size"_L1]);
    a.mimeType = str(j["mimeType"_L1]);
    a.url = str(j["url"_L1]);
    // Trust the daemon's own classification when it sent one.
    a.image = j["image"_L1].isBool() ? j["image"_L1].toBool()
                                     : a.mimeType.startsWith("image/"_L1);
    return a;
}

QJsonObject Attachment::toJson() const
{
    return {{"path"_L1, path}, {"name"_L1, name}, {"size"_L1, size},
            {"mimeType"_L1, mimeType}, {"url"_L1, url}, {"image"_L1, image}};
}

QVariantMap Attachment::toVariant() const
{
    return {{u"path"_s, path}, {u"name"_s, name}, {u"size"_s, size},
            {u"sizeLabel"_s, formatBytes(size)}, {u"mimeType"_s, mimeType},
            {u"url"_s, url}, {u"image"_s, image}};
}

// ── ToolCall ────────────────────────────────────────────────────────────────

ToolCall ToolCall::fromHistory(const QJsonObject &j)
{
    ToolCall t;
    t.id = str(j["id"_L1]);
    t.name = str(j["name"_L1], u"tool"_s);
    t.input = j["input"_L1].toObject();
    t.error = str(j["error"_L1]);
    t.output = str(j["output"_L1]);
    t.done = true;
    t.durationMs = j["durationMs"_L1].isDouble() ? num(j["durationMs"_L1]) : -1;
    t.savedDetail = str(j["detail"_L1]);
    t.savedPurpose = str(j["purpose"_L1]);
    return t;
}

QJsonObject ToolCall::toSnapshot() const
{
    const QVariantMap described = toVariant();
    QJsonObject o{{"id"_L1, id}, {"name"_L1, name}};
    const QString detail = described.value(u"detail"_s).toString();
    const QString purpose = described.value(u"purpose"_s).toString();
    if (!detail.isEmpty())
        o["detail"_L1] = detail;
    if (!purpose.isEmpty())
        o["purpose"_L1] = purpose;
    if (durationMs >= 0)
        o["durationMs"_L1] = durationMs;
    // That it failed, and the first line of why; not the whole output.
    if (!error.isEmpty())
        o["error"_L1] = error.section(u'\n', 0, 0).left(200);
    return o;
}

QVariantMap describeTool(const QString &name, const QJsonObject &input)
{
    QString label = name;
    QString group;
    // MCP tools arrive as mcp__<server>__<tool>.
    if (name.startsWith("mcp__"_L1)) {
        const QStringList parts = name.mid(5).split(u"__"_s);
        if (parts.size() >= 2) {
            group = parts.first();
            label = parts.mid(1).join(u' ');
        }
    }
    label.replace(u'_', u' ');
    if (!label.isEmpty())
        label[0] = label.at(0).toUpper();

    // The argument that says what the call acts on, when there is an
    // obvious one.
    static const QLatin1StringView keys[] = {
        "command"_L1, "cmd"_L1, "file_path"_L1, "path"_L1, "url"_L1, "query"_L1,
        "pattern"_L1, "description"_L1, "prompt"_L1, "text"_L1, "name"_L1, "id"_L1,
    };
    QString detail;
    // For a search, what was looked for says more than where.
    if (toolKind(name) == "search"_L1) {
        for (const auto key : {"pattern"_L1, "query"_L1}) {
            if (detail.isEmpty())
                detail = input[key].toString().simplified();
        }
    }
    for (const auto key : keys) {
        if (!detail.isEmpty())
            break;
        const QJsonValue v = input[key];
        if (v.isString() && !v.toString().trimmed().isEmpty())
            detail = v.toString().simplified();
    }
    if (detail.isEmpty()) {
        for (auto it = input.begin(); it != input.end(); ++it) {
            if (it.value().isString() && !it.value().toString().trimmed().isEmpty()) {
                detail = it.value().toString().simplified();
                break;
            }
        }
    }
    if (detail.size() > 140)
        detail = detail.left(139) + u'…';
    // Several tools take a line from the model saying why it is making the
    // call ("Check which Qt is installed"). That is the model's own account.
    QString purpose = input["description"_L1].toString().simplified();
    if (purpose.size() > 140)
        purpose = purpose.left(139) + u'…';
    return {{u"label"_s, label}, {u"group"_s, group}, {u"detail"_s, detail},
            {u"kind"_s, toolKind(name)}, {u"purpose"_s, purpose}};
}

QString toolKind(const QString &name)
{
    // The tool itself, without the MCP server in front, lower-cased and
    // with the underscores dropped: "mcp__fs__read_file" is "readfile".
    QString tool = name;
    if (name.startsWith("mcp__"_L1)) {
        const QStringList parts = name.mid(5).split(u"__"_s);
        if (parts.size() >= 2)
            tool = parts.mid(1).join(QString());
    }
    tool = tool.toLower().remove(u'_').remove(u'-');
    // Every spelling seen so far, across Claude Code, Codex, opencode and
    // Gemini backends.
    static const QHash<QString, QString> kinds = [] {
        const std::pair<QLatin1StringView, QList<QLatin1StringView>> table[] = {
            {"command"_L1,
             {"bash"_L1, "shell"_L1, "executecommand"_L1, "deviceexec"_L1, "runcommand"_L1,
              "execcommand"_L1, "exec"_L1, "localshell"_L1, "commandexecution"_L1,
              "runterminalcmd"_L1}},
            {"read"_L1,
             {"read"_L1, "notebookread"_L1, "viewfile"_L1, "readfile"_L1, "openfile"_L1,
              "viewcodeitem"_L1, "readmanyfiles"_L1}},
            {"edit"_L1,
             {"write"_L1, "edit"_L1, "multiedit"_L1, "notebookedit"_L1, "writefile"_L1,
              "editfile"_L1, "writetofile"_L1, "createfile"_L1, "applypatch"_L1,
              "replacefilecontent"_L1, "multireplacefilecontent"_L1, "replace"_L1, "patch"_L1}},
            {"search"_L1,
             {"glob"_L1, "grep"_L1, "search"_L1, "grepsearch"_L1, "findbyname"_L1,
              "codebasesearch"_L1, "filesearch"_L1, "listdir"_L1, "listdirectory"_L1, "ls"_L1,
              "searchfiles"_L1}},
            {"web"_L1,
             {"websearch"_L1, "webfetch"_L1, "fetchurl"_L1, "searchweb"_L1, "readurlcontent"_L1,
              "fetch"_L1, "googlesearch"_L1}},
            {"agent"_L1, {"task"_L1, "agent"_L1, "spawnagent"_L1}},
        };
        QHash<QString, QString> out;
        for (const auto &[kind, names] : table) {
            for (const QLatin1StringView spelled : names)
                out.insert(spelled, kind);
        }
        return out;
    }();
    return kinds.value(tool, u"other"_s);
}

QVariantMap summariseTools(const QVariantList &tools)
{
    QVariantList kinds;
    QVariantMap running;
    int finished = 0, failed = 0;
    qint64 spent = 0;
    for (const QVariant &row : tools) {
        const QVariantMap tool = row.toMap();
        const bool bad = tool.value(u"failed"_s).toBool();
        if (bad)
            ++failed;
        if (bad || tool.value(u"done"_s).toBool())
            ++finished;
        else
            running = tool; // the latest one in flight is the one to name
        spent += qMax<qint64>(0, tool.value(u"durationMs"_s).toLongLong());

        const QString kind = tool.value(u"kind"_s).toString();
        bool counted = false;
        for (QVariant &entry : kinds) {
            QVariantMap bucket = entry.toMap();
            if (bucket.value(u"kind"_s).toString() != kind)
                continue;
            bucket[u"count"_s] = bucket.value(u"count"_s).toInt() + 1;
            entry = bucket;
            counted = true;
            break;
        }
        if (!counted) {
            kinds << QVariantMap{{u"kind"_s, kind}, {u"count"_s, 1},
                                 {u"label"_s, tool.value(u"label"_s)}};
        }
    }
    return {
        {u"kinds"_s, kinds},
        {u"running"_s, running},
        {u"finished"_s, finished},
        {u"failed"_s, failed},
        {u"total"_s, int(tools.size())},
        {u"duration"_s, spent > 0 ? formatDuration(spent) : QString()},
    };
}

QVariantMap ToolCall::toVariant() const
{
    QVariantMap m = describeTool(name, input);
    // Restored from the snapshot: the input is gone, its summary is not.
    if (input.isEmpty()) {
        if (!savedDetail.isEmpty())
            m[u"detail"_s] = savedDetail;
        if (!savedPurpose.isEmpty())
            m[u"purpose"_s] = savedPurpose;
    }
    m[u"id"_s] = id;
    m[u"name"_s] = name;
    m[u"done"_s] = done;
    m[u"failed"_s] = !error.isEmpty();
    m[u"error"_s] = error;
    m[u"output"_s] = output;
    m[u"startedMs"_s] = startedMs;
    m[u"duration"_s] = durationMs >= 0 ? formatDuration(durationMs) : QString();
    m[u"durationMs"_s] = durationMs;
    m[u"input"_s] = input.isEmpty()
        ? QString()
        : QString::fromUtf8(QJsonDocument(input).toJson(QJsonDocument::Indented)).trimmed();
    return m;
}

// ── Message ─────────────────────────────────────────────────────────────────

Message Message::fromJson(const QJsonObject &j)
{
    Message m;
    m.id = str(j["id"_L1]);
    m.chatId = str(j["chatId"_L1]);
    const QString role = j["role"_L1].toString();
    m.role = role == "assistant"_L1 ? Assistant : role == "system"_L1 ? System : User;
    m.text = str(j["text"_L1]);
    m.ts = num(j["ts"_L1]);
    m.buttons = j["buttons"_L1].toArray();
    for (const auto &r : j["reactions"_L1].toArray())
        m.reactions << str(r);
    m.imagePath = j["imagePath"_L1].toString();
    for (const auto &a : j["attachments"_L1].toArray())
        m.attachments << Attachment::fromJson(a.toObject());
    for (const auto &t : j["tools"_L1].toArray())
        m.tools << ToolCall::fromHistory(t.toObject());
    m.durationMs = qMax<qint64>(0, num(j["durationMs"_L1]));
    m.tokensIn = qMax<qint64>(0, num(j["tokensIn"_L1]));
    m.tokensOut = qMax<qint64>(0, num(j["tokensOut"_L1]));
    return m;
}

QJsonObject Message::toSnapshot() const
{
    QJsonObject o{
        {"id"_L1, id},
        {"chatId"_L1, chatId},
        {"role"_L1, role == Assistant ? u"assistant"_s : role == System ? u"system"_s : u"user"_s},
        {"text"_L1, text},
        {"ts"_L1, ts},
    };
    if (!imagePath.isEmpty())
        o["imagePath"_L1] = imagePath;
    if (!attachments.isEmpty()) {
        QJsonArray arr;
        for (const Attachment &a : attachments)
            arr << a.toJson();
        o["attachments"_L1] = arr;
    }
    if (!tools.isEmpty()) {
        QJsonArray arr;
        for (const ToolCall &t : tools)
            arr << t.toSnapshot();
        o["tools"_L1] = arr;
    }
    if (durationMs > 0)
        o["durationMs"_L1] = durationMs;
    if (tokensIn > 0)
        o["tokensIn"_L1] = tokensIn;
    if (tokensOut > 0)
        o["tokensOut"_L1] = tokensOut;
    return o;
}

bool Message::serverId(qint64 *out) const
{
    bool ok = false;
    const qint64 n = id.toLongLong(&ok);
    if (ok && out)
        *out = n;
    return ok;
}

bool messageLess(const Message &a, const Message &b)
{
    if (a.ts != b.ts)
        return a.ts < b.ts;
    qint64 na = 0, nb = 0;
    const bool ha = a.serverId(&na), hb = b.serverId(&nb);
    if (ha && hb)
        return na < nb;
    if (ha != hb)
        return ha;
    return a.id < b.id;
}

// ── Chat ────────────────────────────────────────────────────────────────────

Chat Chat::fromJson(const QJsonObject &j)
{
    Chat c;
    c.id = str(j["id"_L1]);
    c.title = unescapeHtml(str(j["title"_L1], u"New chat"_s));
    c.createdAt = num(j["createdAt"_L1]);
    c.lastActive = num(j["lastActive"_L1]);
    c.preview = unescapeHtml(str(j["preview"_L1]));
    c.model = j["model"_L1].toString();
    c.backend = j["backend"_L1].toString();
    c.effort = j["effort"_L1].toString();
    c.pulse = j["pulse"_L1].toBool();
    if (j["context"_L1].isObject()) {
        const QJsonObject x = j["context"_L1].toObject();
        c.contextKnown = x["known"_L1].toBool();
        c.contextUsed = num(x["used"_L1]);
        c.contextMax = num(x["max"_L1]);
        c.contextPct = int(num(x["pct"_L1]));
        c.contextWarn = x["warn"_L1].toBool();
    }
    if (j["queued"_L1].isObject()) {
        const QJsonObject q = j["queued"_L1].toObject();
        c.hasQueued = true;
        c.queuedText = str(q["text"_L1]);
        c.queuedAttachments = int(num(q["attachmentCount"_L1],
                                      q["hasAttachment"_L1].toBool() ? 1 : 0));
    }
    return c;
}

QJsonObject Chat::toSnapshot() const
{
    QJsonObject o{{"id"_L1, id}, {"title"_L1, title}, {"createdAt"_L1, createdAt},
                  {"lastActive"_L1, lastActive}, {"preview"_L1, preview}};
    if (!model.isEmpty())
        o["model"_L1] = model;
    if (!backend.isEmpty())
        o["backend"_L1] = backend;
    if (!effort.isEmpty())
        o["effort"_L1] = effort;
    return o;
}

QVariantMap Chat::toVariant() const
{
    return {
        {u"id"_s, id},
        {u"title"_s, title},
        {u"preview"_s, preview},
        {u"lastActive"_s, lastActive},
        {u"model"_s, model},
        {u"backend"_s, backend},
        {u"effort"_s, effort},
        {u"pulse"_s, pulse},
        {u"contextKnown"_s, contextKnown},
        {u"contextUsed"_s, contextUsed},
        {u"contextMax"_s, contextMax},
        {u"contextPct"_s, contextPct},
        {u"contextWarn"_s, contextWarn},
        {u"contextLabel"_s, contextKnown
             ? u"%1 of %2 tokens"_s.arg(formatCount(contextUsed), formatCount(contextMax))
             : QString()},
        {u"hasQueued"_s, hasQueued},
        {u"queuedText"_s, queuedText},
        {u"queuedAttachments"_s, queuedAttachments},
    };
}

// ── ChatListModel ───────────────────────────────────────────────────────────

ChatListModel::ChatListModel(QObject *parent) : QAbstractListModel(parent) {}

int ChatListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_chats.size());
}

QString ChatListModel::sectionFor(qint64 lastActive)
{
    const QDate day = QDateTime::fromMSecsSinceEpoch(lastActive).date();
    const QDate today = QDate::currentDate();
    const qint64 age = day.daysTo(today);
    if (age <= 0)
        return QCoreApplication::translate("Sidebar", "Today");
    if (age == 1)
        return QCoreApplication::translate("Sidebar", "Yesterday");
    if (age < 7)
        return QCoreApplication::translate("Sidebar", "Previous 7 days");
    if (age < 30)
        return QCoreApplication::translate("Sidebar", "Previous 30 days");
    if (day.year() == today.year())
        return QLocale().monthName(day.month());
    return QString::number(day.year());
}

QVariant ChatListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_chats.size())
        return {};
    const Chat &c = m_chats.at(index.row());
    switch (role) {
    case IdRole: return c.id;
    case TitleRole: return c.title;
    case PreviewRole: return c.preview;
    case LastActiveRole: return c.lastActive;
    case SectionRole:
        // Pinned chats lead the list under a heading of their own, except
        // in the archive, which is one list by date.
        if (isPinned && isPinned(c) && !(isArchived && isArchived(c)))
            return QCoreApplication::translate("Sidebar", "Pinned");
        return sectionFor(c.lastActive);
    case UnreadRole: return isUnread ? isUnread(c) : false;
    case RunningRole: return isRunning ? isRunning(c) : false;
    case QueuedRole: return c.hasQueued;
    case PinnedRole: return isPinned ? isPinned(c) : false;
    case ArchivedRole: return isArchived ? isArchived(c) : false;
    }
    return {};
}

QHash<int, QByteArray> ChatListModel::roleNames() const
{
    return {{IdRole, "chatId"}, {TitleRole, "title"}, {PreviewRole, "preview"},
            {LastActiveRole, "lastActive"}, {SectionRole, "section"},
            {UnreadRole, "unread"}, {RunningRole, "running"}, {QueuedRole, "queued"},
            {PinnedRole, "pinned"}, {ArchivedRole, "archived"}};
}

int ChatListModel::indexOf(const QString &id) const
{
    for (int i = 0; i < m_chats.size(); ++i) {
        if (m_chats.at(i).id == id)
            return i;
    }
    return -1;
}

const Chat *ChatListModel::find(const QString &id) const
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_chats.at(i);
}

Chat *ChatListModel::find(const QString &id)
{
    const int i = indexOf(id);
    return i < 0 ? nullptr : &m_chats[i];
}

void ChatListModel::setChats(QList<Chat> chats)
{
    std::stable_sort(chats.begin(), chats.end(),
                     [](const Chat &a, const Chat &b) { return a.lastActive > b.lastActive; });
    beginResetModel();
    m_chats = std::move(chats);
    endResetModel();
}

void ChatListModel::upsert(const Chat &chat)
{
    int from = indexOf(chat.id);
    if (from < 0) {
        int to = 0;
        while (to < m_chats.size() && m_chats.at(to).lastActive > chat.lastActive)
            ++to;
        beginInsertRows({}, to, to);
        m_chats.insert(to, chat);
        endInsertRows();
        return;
    }
    m_chats[from] = chat;
    // Most-recent-first: find where it belongs among the others.
    int to = 0;
    for (int i = 0; i < m_chats.size(); ++i) {
        if (i != from && m_chats.at(i).lastActive > chat.lastActive)
            ++to;
    }
    if (to != from) {
        beginMoveRows({}, from, from, {}, to > from ? to + 1 : to);
        m_chats.move(from, to);
        endMoveRows();
    }
    const QModelIndex idx = index(to);
    emit dataChanged(idx, idx);
}

bool ChatListModel::remove(const QString &id)
{
    const int i = indexOf(id);
    if (i < 0)
        return false;
    beginRemoveRows({}, i, i);
    m_chats.removeAt(i);
    endRemoveRows();
    return true;
}

void ChatListModel::clear()
{
    beginResetModel();
    m_chats.clear();
    endResetModel();
}

void ChatListModel::touch(const QString &id)
{
    if (m_chats.isEmpty())
        return;
    if (id.isEmpty()) {
        emit dataChanged(index(0), index(int(m_chats.size()) - 1),
                         {UnreadRole, RunningRole, PinnedRole, ArchivedRole, SectionRole});
        return;
    }
    const int i = indexOf(id);
    if (i >= 0)
        emit dataChanged(index(i), index(i),
                         {UnreadRole, RunningRole, PinnedRole, ArchivedRole, SectionRole});
}

ChatFilterModel::ChatFilterModel(QObject *parent) : QSortFilterProxyModel(parent)
{
    connect(this, &QAbstractItemModel::rowsInserted, this, &ChatFilterModel::countChanged);
    connect(this, &QAbstractItemModel::rowsRemoved, this, &ChatFilterModel::countChanged);
    connect(this, &QAbstractItemModel::modelReset, this, &ChatFilterModel::countChanged);
    connect(this, &QAbstractItemModel::layoutChanged, this, &ChatFilterModel::countChanged);
    setDynamicSortFilter(true);
    sort(0);
}

void ChatFilterModel::setArchived(bool archived)
{
    if (archived == m_archived)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_archived = archived;
    endFilterChange();
#else
    m_archived = archived;
    invalidateFilter();
#endif
    emit filterChanged();
    emit countChanged();
}

bool ChatFilterModel::lessThan(const QModelIndex &left, const QModelIndex &right) const
{
    // Pinned first (outside the archive), then most recent first.
    const bool lp = !m_archived && left.data(ChatListModel::PinnedRole).toBool();
    const bool rp = !m_archived && right.data(ChatListModel::PinnedRole).toBool();
    if (lp != rp)
        return lp;
    return left.data(ChatListModel::LastActiveRole).toLongLong()
        > right.data(ChatListModel::LastActiveRole).toLongLong();
}

void ChatFilterModel::setFilter(const QString &filter)
{
    const QString f = filter.trimmed();
    if (f == m_filter)
        return;
#if QT_VERSION >= QT_VERSION_CHECK(6, 10, 0)
    beginFilterChange();
    m_filter = f;
    endFilterChange();
#else
    m_filter = f;
    invalidateFilter();
#endif
    emit filterChanged();
    emit countChanged();
}

bool ChatFilterModel::filterAcceptsRow(int row, const QModelIndex &parent) const
{
    const QModelIndex idx = sourceModel()->index(row, 0, parent);
    if (idx.data(ChatListModel::ArchivedRole).toBool() != m_archived)
        return false;
    if (m_filter.isEmpty())
        return true;
    return idx.data(ChatListModel::TitleRole).toString().contains(m_filter, Qt::CaseInsensitive)
        || idx.data(ChatListModel::PreviewRole).toString().contains(m_filter, Qt::CaseInsensitive);
}

// ── MessageListModel ────────────────────────────────────────────────────────

MessageListModel::MessageListModel(QObject *parent) : QAbstractListModel(parent) {}

int MessageListModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() || !m_rows ? 0 : int(m_rows->size());
}

QHash<int, QByteArray> MessageListModel::roleNames() const
{
    return {{IdRole, "messageId"}, {RoleRole, "role"}, {TextRole, "text"}, {TsRole, "ts"},
            {BlocksRole, "blocks"}, {ImagesRole, "images"}, {FilesRole, "files"},
            {ButtonsRole, "buttons"}, {ReactionsRole, "reactions"}, {ToolsRole, "tools"},
            {StatsRole, "stats"}, {DayRole, "day"}};
}

QVariant MessageListModel::data(const QModelIndex &index, int role) const
{
    if (!m_rows || !index.isValid() || index.row() >= m_rows->size())
        return {};
    const Message &m = m_rows->at(index.row());
    switch (role) {
    case IdRole: return m.id;
    case RoleRole:
        return m.role == Message::Assistant ? u"assistant"_s
             : m.role == Message::System ? u"system"_s : u"user"_s;
    case TextRole: return m.text;
    case TsRole: return m.ts;
    case BlocksRole:
        if (!m.blocksValid) {
            m.blocks = markdown::split(m.text);
            m.blocksValid = true;
        }
        return m.blocks;
    case ImagesRole: {
        QVariantList out;
        for (const Attachment &a : m.attachments) {
            if (a.image)
                out << a.toVariant();
        }
        // A daemon older than multi-file attachments sends only imagePath.
        if (out.isEmpty() && !m.imagePath.isEmpty()) {
            out << QVariantMap{{u"url"_s, m.imagePath}, {u"name"_s, u"image"_s},
                               {u"image"_s, true}, {u"path"_s, QString()},
                               {u"mimeType"_s, u"image/*"_s}, {u"sizeLabel"_s, QString()}};
        }
        return out;
    }
    case FilesRole: {
        QVariantList out;
        for (const Attachment &a : m.attachments) {
            if (!a.image)
                out << a.toVariant();
        }
        return out;
    }
    case ButtonsRole: return m.buttons.toVariantList();
    case ReactionsRole: return m.reactions;
    case ToolsRole: {
        QVariantList out;
        for (const ToolCall &t : m.tools)
            out << t.toVariant();
        return out;
    }
    case StatsRole: {
        QStringList parts;
        if (m.durationMs > 0)
            parts << formatDuration(m.durationMs);
        if (m.tokensIn > 0)
            parts << u"%1 in"_s.arg(formatCount(m.tokensIn));
        if (m.tokensOut > 0)
            parts << u"%1 out"_s.arg(formatCount(m.tokensOut));
        return parts.join(u" · "_s);
    }
    case DayRole: {
        // Non-empty on the first message of each calendar day.
        const QDate day = QDateTime::fromMSecsSinceEpoch(m.ts).date();
        if (index.row() > 0
            && QDateTime::fromMSecsSinceEpoch(m_rows->at(index.row() - 1).ts).date() == day)
            return QString();
        const qint64 age = day.daysTo(QDate::currentDate());
        if (age == 0)
            return u"Today"_s;
        if (age == 1)
            return u"Yesterday"_s;
        return QLocale().toString(day, age < 300 ? u"dddd, d MMMM"_s : u"d MMMM yyyy"_s);
    }
    }
    return {};
}

void MessageListModel::bind(QList<Message> *rows)
{
    beginResetModel();
    m_rows = rows;
    endResetModel();
    emit countChanged();
}

void MessageListModel::reset(const std::function<void()> &mutate)
{
    beginResetModel();
    mutate();
    endResetModel();
    emit countChanged();
}

void MessageListModel::insert(int first, int count, const std::function<void()> &mutate)
{
    if (count <= 0) {
        mutate();
        return;
    }
    beginInsertRows({}, first, first + count - 1);
    mutate();
    endInsertRows();
    // The row after an insertion may no longer start its day.
    const int next = first + count;
    if (m_rows && next < m_rows->size())
        emit dataChanged(index(next), index(next), {DayRole});
    emit countChanged();
}

void MessageListModel::remove(int row, const std::function<void()> &mutate)
{
    beginRemoveRows({}, row, row);
    mutate();
    endRemoveRows();
    if (m_rows && row < m_rows->size())
        emit dataChanged(index(row), index(row), {DayRole});
    emit countChanged();
}

void MessageListModel::changed(int row)
{
    if (m_rows && row >= 0 && row < m_rows->size())
        emit dataChanged(index(row), index(row));
}

// ── TurnState ───────────────────────────────────────────────────────────────

void TurnState::Data::reset(qint64 now)
{
    active = true;
    typing = true;
    continuing = false;
    startedMs = now;
    clearStream();
}

void TurnState::Data::clearStream()
{
    draft.clear();
    reasoning.clear();
    tools.clear();
}

TurnState::TurnState(QObject *parent) : QObject(parent)
{
    m_frame.setSingleShot(true);
    m_frame.setInterval(33);
    connect(&m_frame, &QTimer::timeout, this, &TurnState::changed);
    connect(this, &TurnState::changed, this, &TurnState::syncTools);
}

void TurnState::show(const Data *data)
{
    m_data = data;
    flush();
}

void TurnState::poke()
{
    if (m_frame.isActive())
        return;
    // Thirty frames a second while a reply is short. A long one takes
    // longer to lay out and is read no faster for being redrawn so often:
    // it eases down to a dozen.
    const qsizetype written = m_data ? m_data->draft.size() : 0;
    m_frame.start(int(qMin<qsizetype>(33 + written / 400, 85)));
}

void TurnState::flush()
{
    m_frame.stop();
    emit changed();
}

QVariantList TurnState::blocks() const
{
    if (!m_data || m_data->draft.isEmpty()) {
        m_blocks.clear();
        m_blocksOf.clear();
        return {};
    }
    if (m_blocksOf != m_data->draft) {
        m_blocksOf = m_data->draft;
        m_blocks = markdown::split(m_blocksOf);
    }
    return m_blocks;
}

size_t TurnState::toolsMark() const
{
    if (!m_data)
        return 0;
    // Everything a row shows that can change after the call is listed.
    size_t mark = qHash(m_data->tools.size(), size_t(m_data));
    for (const ToolCall &t : m_data->tools) {
        mark = qHashMulti(mark, t.id, t.done, t.error.size(), t.output.size(), t.durationMs,
                          t.input.size());
    }
    return mark | 1; // never the "no turn" mark
}

void TurnState::syncTools()
{
    const size_t mark = toolsMark();
    if (mark == m_toolsAnnounced)
        return;
    m_toolsAnnounced = mark;
    emit toolsChanged();
}

QVariantList TurnState::tools() const
{
    // Built once per change, not once per reader per frame.
    const size_t mark = toolsMark();
    if (mark != m_toolRowsMark) {
        m_toolRowsMark = mark;
        m_toolRows.clear();
        if (m_data) {
            for (const ToolCall &t : m_data->tools)
                m_toolRows << t.toVariant();
        }
    }
    return m_toolRows;
}

} // namespace talos
