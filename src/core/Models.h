// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QAbstractListModel>
#include <QJsonArray>
#include <QJsonObject>
#include <QSortFilterProxyModel>
#include <QTimer>
#include <QVariant>
#include <functional>

namespace talos {

// ── Wire types (Bridge Protocol v1) ─────────────────────────────────────────
// Parsing is tolerant by design: unknown fields are ignored and a missing or
// mistyped one falls back to a neutral value, because the protocol evolves
// additively and an older or newer daemon must still render.

struct Attachment {
    QString path;     ///< where the file lives on the daemon host
    QString name;
    qint64 size = 0;
    QString mimeType;
    QString url;      ///< relative bridge path the bytes are served from
    bool image = false;

    static Attachment fromJson(const QJsonObject &j);
    QJsonObject toJson() const;
    QVariantMap toVariant() const;
};

struct ToolCall {
    QString id;
    QString name;
    QJsonObject input;
    QString error;
    QString output;
    bool done = false;
    qint64 startedMs = 0;
    qint64 durationMs = -1; ///< -1 while unknown

    static ToolCall fromHistory(const QJsonObject &j);
    QVariantMap toVariant() const;
};

struct Message {
    enum Role { User, Assistant, System };

    QString id;
    QString chatId;
    Role role = User;
    QString text;
    qint64 ts = 0;
    QJsonArray buttons;
    QStringList reactions;
    QString imagePath;
    QList<Attachment> attachments;
    QList<ToolCall> tools;
    qint64 durationMs = 0;
    qint64 tokensIn = 0;
    qint64 tokensOut = 0;

    // Parsed markdown, cached until the text changes.
    mutable QVariantList blocks;
    mutable bool blocksValid = false;

    static Message fromJson(const QJsonObject &j);
    QJsonObject toSnapshot() const;
    /// Server-assigned ids are numeric; local notes are not.
    bool serverId(qint64 *out) const;
};

/// Display order: oldest first by timestamp, ties broken by server id, with
/// local notes after server rows. A total order.
bool messageLess(const Message &a, const Message &b);

struct Chat {
    QString id;
    QString title;
    qint64 createdAt = 0;
    qint64 lastActive = 0;
    QString preview;
    QString model;
    QString backend;
    QString effort;
    bool pulse = false;

    bool contextKnown = false;
    qint64 contextUsed = 0;
    qint64 contextMax = 0;
    int contextPct = 0;
    bool contextWarn = false;

    bool hasQueued = false;
    QString queuedText;
    int queuedAttachments = 0;

    static Chat fromJson(const QJsonObject &j);
    QJsonObject toSnapshot() const;
    QVariantMap toVariant() const;
};

QString unescapeHtml(const QString &text);
QString formatBytes(qint64 bytes);
QString formatDuration(qint64 ms);
QString formatCount(qint64 n);
/// A short human label for a tool call: what it is and what it acts on.
QVariantMap describeTool(const QString &name, const QJsonObject &input);

// ── Chat list ───────────────────────────────────────────────────────────────

class ChatListModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        TitleRole,
        PreviewRole,
        LastActiveRole,
        SectionRole,
        UnreadRole,
        RunningRole,
        QueuedRole,
    };

    explicit ChatListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    const QList<Chat> &chats() const { return m_chats; }
    const Chat *find(const QString &id) const;
    Chat *find(const QString &id);
    void setChats(QList<Chat> chats);
    void upsert(const Chat &chat);
    bool remove(const QString &id);
    void clear();
    /// Re-read the decorations (unread, running) of one chat, or of all.
    void touch(const QString &id = {});

    std::function<bool(const Chat &)> isUnread;
    std::function<bool(const Chat &)> isRunning;

    static QString sectionFor(qint64 lastActive);

private:
    int indexOf(const QString &id) const;
    QList<Chat> m_chats;
};

class ChatFilterModel : public QSortFilterProxyModel
{
    Q_OBJECT
    Q_PROPERTY(QString filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    explicit ChatFilterModel(QObject *parent = nullptr);
    QString filter() const { return m_filter; }
    void setFilter(const QString &filter);
    int count() const { return rowCount(); }
signals:
    void filterChanged();
    void countChanged();
protected:
    bool filterAcceptsRow(int row, const QModelIndex &parent) const override;
private:
    QString m_filter;
};

// ── Messages of the open chat ───────────────────────────────────────────────

class MessageListModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        RoleRole,
        TextRole,
        TsRole,
        BlocksRole,
        ImagesRole,
        FilesRole,
        ButtonsRole,
        ReactionsRole,
        ToolsRole,
        StatsRole,
        DayRole,
    };

    explicit MessageListModel(QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return int(m_rows ? m_rows->size() : 0); }

    /// Point the model at a chat's message list (owned by the session).
    void bind(QList<Message> *rows);
    const QList<Message> *rows() const { return m_rows; }
    // Mutations of the bound list go through these so views stay in step.
    void reset(const std::function<void()> &mutate);
    void insert(int first, int count, const std::function<void()> &mutate);
    void remove(int row, const std::function<void()> &mutate);
    void changed(int row);

signals:
    void countChanged();

private:
    QList<Message> *m_rows = nullptr;
};

// ── The live turn of the open chat ──────────────────────────────────────────

/// Transient state of a running turn: the streaming draft, the model's
/// reasoning and its live tool calls. Changes are coalesced so a burst of
/// streamed tokens repaints at most once per frame.
class TurnState : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool active READ active NOTIFY changed)
    Q_PROPERTY(bool typing READ typing NOTIFY changed)
    Q_PROPERTY(bool continuing READ continuing NOTIFY changed)
    Q_PROPERTY(QString text READ text NOTIFY changed)
    Q_PROPERTY(QVariantList blocks READ blocks NOTIFY changed)
    Q_PROPERTY(QString reasoning READ reasoning NOTIFY changed)
    Q_PROPERTY(QVariantList tools READ tools NOTIFY changed)
    Q_PROPERTY(qint64 startedMs READ startedMs NOTIFY changed)
public:
    struct Data {
        bool active = false;
        bool typing = false;
        bool continuing = false;
        QString draft;
        QString reasoning;
        QList<ToolCall> tools;
        qint64 startedMs = 0;

        bool hasContent() const { return !draft.isEmpty() || !reasoning.isEmpty() || !tools.isEmpty(); }
        void reset(qint64 now);
        void clearStream();
    };

    explicit TurnState(QObject *parent = nullptr);

    void show(const Data *data);
    /// Something streamed in: repaint on the next frame.
    void poke();
    /// Shape changed (a row appeared or vanished): repaint now.
    void flush();

    bool active() const { return m_data && m_data->active; }
    bool typing() const { return m_data && m_data->typing; }
    bool continuing() const { return m_data && m_data->continuing; }
    QString text() const { return m_data ? m_data->draft : QString(); }
    QVariantList blocks() const;
    QString reasoning() const { return m_data ? m_data->reasoning : QString(); }
    QVariantList tools() const;
    qint64 startedMs() const { return m_data ? m_data->startedMs : 0; }

signals:
    void changed();

private:
    const Data *m_data = nullptr;
    QTimer m_frame;
};

} // namespace talos
