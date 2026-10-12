// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/BridgeClient.h"
#include "core/Models.h"
#include "core/Store.h"

#include <QJSValue>
#include <QObject>
#include <QSet>
#include <QTimer>
#include <map>

class QJSEngine;

namespace talos {

/// The client's single source of truth: owns the connection to one Talon
/// bridge, the chats and their messages, and the live turn of each chat.
///
/// Connection attempts are numbered. Every asynchronous step captures the
/// number it started under and stops if a newer attempt has begun, so a
/// reply that arrives late can never overwrite the state of the attempt
/// that replaced it.
class Session : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int conn READ conn NOTIFY connChanged)
    Q_PROPERTY(QString connError READ connError NOTIFY connChanged)
    Q_PROPERTY(bool connFatal READ connFatal NOTIFY connChanged)
    Q_PROPERTY(QString connNote READ connNote NOTIFY connChanged)
    Q_PROPERTY(QVariantMap status READ status NOTIFY statusChanged)
    Q_PROPERTY(QString botName READ botName NOTIFY statusChanged)
    Q_PROPERTY(QVariantMap endpoint READ endpointVariant NOTIFY endpointChanged)
    Q_PROPERTY(talos::ChatFilterModel *chats READ chats CONSTANT)
    Q_PROPERTY(int chatCount READ chatCount NOTIFY chatCountChanged)
    Q_PROPERTY(talos::MessageListModel *messages READ messages CONSTANT)
    Q_PROPERTY(talos::TurnState *turn READ turn CONSTANT)
    Q_PROPERTY(QString selectedChatId READ selectedChatId NOTIFY selectionChanged)
    Q_PROPERTY(QVariantMap chat READ chat NOTIFY chatChanged)
    Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY historyChanged)
    Q_PROPERTY(bool loadingOlder READ loadingOlder NOTIFY historyChanged)
    Q_PROPERTY(bool hasMoreHistory READ hasMoreHistory NOTIFY historyChanged)
    Q_PROPERTY(QVariantList models READ models NOTIFY modelsChanged)
    Q_PROPERTY(QVariantList commands READ commands NOTIFY commandsChanged)
    Q_PROPERTY(int archivedCount READ archivedCount NOTIFY chatMarksChanged)
    Q_PROPERTY(QVariantMap daemonConfig READ daemonConfig NOTIFY daemonConfigChanged)
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(QVariantList locations READ locations NOTIFY devicesChanged)
    Q_PROPERTY(bool narrow READ narrow WRITE setNarrow NOTIFY narrowChanged)
public:
    enum Conn { Idle, Connecting, Connected, Error };
    Q_ENUM(Conn)

    static constexpr int kHistoryInitial = 200;
    static constexpr int kHistoryPage = 100;
    static constexpr int kSnapshotMessages = 30;
    /// Grace after a mid-turn reply before "still working" shows, so a turn
    /// whose end lands right behind its only reply never flashes it.
    static constexpr int kContinuingGraceMs = 600;

    /// @p narrow is the layout the app starts in: with one pane, no chat is
    /// opened on the user's behalf.
    explicit Session(Store *store, QObject *parent = nullptr, bool narrow = false);
    ~Session() override;

    void setScriptEngine(QJSEngine *engine) { m_engine = engine; }
    /// The background service keeps no chat cache.
    void setSnapshotsEnabled(bool on) { m_snapshots = on; }

    // ── State ───────────────────────────────────────────────────────────────
    int conn() const { return m_conn; }
    QString connError() const { return m_connError; }
    bool connFatal() const { return m_connFatal; }
    QString connNote() const { return m_connNote; }
    QVariantMap status() const { return m_status; }
    QString botName() const;
    QVariantMap endpointVariant() const;
    ChatFilterModel *chats() { return &m_chatFilter; }
    ChatListModel *chatList() { return &m_chats; }
    int chatCount() const { return m_chats.rowCount(); }
    MessageListModel *messages() { return &m_messages; }
    TurnState *turn() { return &m_turn; }
    QString selectedChatId() const { return m_selected; }
    QVariantMap chat() const;
    bool historyLoading() const;
    bool loadingOlder() const;
    bool hasMoreHistory() const;
    QVariantList models() const { return m_models; }
    QVariantList commands() const { return m_commands; }
    QVariantMap daemonConfig() const { return m_daemonConfig; }
    QVariantList devices() const { return m_devices; }
    QVariantList locations() const { return m_locations; }
    bool narrow() const { return m_narrow; }
    void setNarrow(bool narrow);

    BridgeClient *client() const { return m_client; }
    /// The endpoint in use, which in local mode carries the discovered port
    /// and token the saved profile does not.
    Profile activeEndpoint() const;
    /// Take the fallback addresses a mesh registration reply advertised.
    void learnEndpoints(const QJsonObject &advertised);
    /// The daemon hinted, outside a connect, that the credential should be
    /// traded or rotated.
    void checkCredential();
    /// How often, while on a fallback, the configured address is tried again.
    void setFailbackIntervalMs(int ms) { m_failback.setInterval(ms); }
    bool hasCapability(const QString &name) const;
    bool isTurnRunning(const QString &chatId) const;
    const QList<Message> *messagesOf(const QString &chatId) const;

    // ── Connection ──────────────────────────────────────────────────────────
    Q_INVOKABLE void start();
    Q_INVOKABLE void stop();
    /// The app came back to the foreground: reopen a stream that went quiet.
    Q_INVOKABLE void resume();
    /// Paint from the offline snapshot when nothing is loaded yet (the app
    /// lock just released the key that seals it).
    void restoreSnapshot() { hydrate(); }
    /// Persist the offline snapshot now (app hidden or quitting).
    Q_INVOKABLE void persist();
    /// Save @p profile as the active one and connect to it from scratch.
    void applyProfile(const Profile &profile);
    Q_INVOKABLE void switchProfile(const QString &id);
    /// Drop every profile, credential and cached chat: back to first run.
    Q_INVOKABLE void forgetEverything();

    // ── Chats ───────────────────────────────────────────────────────────────
    Q_INVOKABLE void selectChat(const QString &chatId);
    Q_INVOKABLE void clearSelection();
    Q_INVOKABLE void newChat();
    Q_INVOKABLE void renameChat(const QString &chatId, const QString &title);
    Q_INVOKABLE void deleteChat(const QString &chatId);
    Q_INVOKABLE void resetChat(const QString &chatId);
    Q_INVOKABLE void setPulse(const QString &chatId, bool on);
    /// Pinning and archiving are this device's own bookkeeping.
    Q_INVOKABLE void setPinned(const QString &chatId, bool on);
    Q_INVOKABLE void setArchived(const QString &chatId, bool on);
    Q_INVOKABLE bool isPinned(const QString &chatId) const;
    Q_INVOKABLE bool isArchived(const QString &chatId) const;
    int archivedCount() const;
    /// Have the chat list read its headings again (the language changed).
    void refreshChatList() { m_chats.touch(); }
    /// The text of the open chat's most recent message from the user.
    Q_INVOKABLE QString lastUserMessage() const;
    Q_INVOKABLE void refresh();
    Q_INVOKABLE void loadOlder();
    Q_INVOKABLE QString exportMarkdown(const QString &chatId) const;
    Q_INVOKABLE QVariantList chatSummaries() const;

    // ── Turns ───────────────────────────────────────────────────────────────
    /// Send to the open chat. @p attachments are records from uploads. The
    /// callback gets `true` once the daemon accepted it.
    Q_INVOKABLE void send(const QString &text, const QVariantList &attachments, QJSValue done);
    /// Send text to a chat that need not be the open one (a reply typed
    /// into a notification).
    Q_INVOKABLE void sendTo(const QString &chatId, const QString &text);
    /// Open a fresh chat and put a question to it.
    Q_INVOKABLE void askInNewChat(const QString &text);
    /// The open chat's newest message, as `{role, text, blocks}`.
    Q_INVOKABLE QVariantMap lastMessage() const;
    Q_INVOKABLE void interrupt();
    /// Replace or (with empty text) clear the open chat's queued follow-up.
    Q_INVOKABLE void editQueued(const QString &text);
    /// Start streaming a local file up. Returns an id for the progress signals.
    Q_INVOKABLE QString upload(const QUrl &file);
    Q_INVOKABLE QString uploadClipboardImage();
    Q_INVOKABLE void cancelUpload(const QString &uploadId);

    // ── Per-chat options ────────────────────────────────────────────────────
    Q_INVOKABLE void fetchChatOptions(const QString &chatId, QJSValue done);
    Q_INVOKABLE void setModel(const QString &chatId, const QString &model);
    Q_INVOKABLE void setEffort(const QString &chatId, const QString &effort);
    Q_INVOKABLE void setBackend(const QString &chatId, const QString &backend, QJSValue done);

    // ── Daemon surfaces ─────────────────────────────────────────────────────
    /// Full-text search on the daemon. An empty @p chatId searches every chat.
    Q_INVOKABLE void search(const QString &query, const QString &chatId, QJSValue done);
    Q_INVOKABLE void loadConfig();
    Q_INVOKABLE void updateConfig(const QVariantMap &patch, QJSValue done);
    Q_INVOKABLE void control(const QString &action, QJSValue done);
    Q_INVOKABLE void fetchPlugins(QJSValue done);
    Q_INVOKABLE void togglePlugin(const QString &name, bool enabled, QJSValue done);
    Q_INVOKABLE void fetchSkills(QJSValue done);
    Q_INVOKABLE void toggleSkill(const QString &name, bool enabled, QJSValue done);
    Q_INVOKABLE void fetchLogs(int lines, const QString &minLevel, const QString &component,
                               QJSValue done);
    Q_INVOKABLE void fetchMemory(const QString &query, const QString &kind, int limit,
                                 QJSValue done);
    Q_INVOKABLE void fetchMemoryWhy(int id, QJSValue done);
    Q_INVOKABLE void refreshDevices();
    Q_INVOKABLE void whoami(QJSValue done);

    /// Append a local note to a chat (errors, refusals).
    void note(const QString &chatId, const QString &text);
    /// Carry out one of the client's own slash commands. False when the
    /// text isn't one, and should be sent as a message.
    bool runBuiltInCommand(const QString &chatId, const QString &text);
    void post(const QString &chatId, const QString &text, const QVariantList &attachments,
              QJSValue done);

signals:
    void connChanged();
    void statusChanged();
    void endpointChanged();
    void chatCountChanged();
    void selectionChanged();
    void chatChanged();
    void historyChanged();
    void modelsChanged();
    void commandsChanged();
    void chatMarksChanged();
    /// A typed command asked for the model and effort picker.
    void modelPickerRequested();
    void daemonConfigChanged();
    void devicesChanged();
    void narrowChanged();

    /// A new stream is live on @p client (mesh attaches here).
    void clientReady(talos::BridgeClient *client);
    void clientGone();
    /// A message was delivered live (not from history).
    void messageArrived(const QString &chatId, const QString &chatTitle,
                        const QString &text, bool fromAssistant);
    void turnEnded(const QString &chatId);
    void uploadProgress(const QString &uploadId, double fraction);
    void uploadFinished(const QString &uploadId, const QVariantMap &attachment);
    void uploadFailed(const QString &uploadId, const QString &error);
    /// Something the user should see once (toast).
    void notice(const QString &text);
    void olderLoaded(int count);

private:
    struct ChatData {
        QList<Message> messages;
        TurnState::Data turn;
        bool loaded = false;
        bool exhausted = false;
        bool loading = false;
        bool loadingOlder = false;
        int continuingGen = 0;
        /// Messages delivered live while a history fetch was in flight: the
        /// only local rows that may outlive the fetched window.
        QSet<QString> arrivedDuringLoad;
    };

    // Connection
    void connectTo(const Profile &endpoint, int epoch, bool persistPin);
    void onStreamOpen(int epoch, bool persistPin);
    void onStreamClosed(const ApiResult &why);
    void fail(const ApiResult &why);
    void setConn(Conn conn, const QString &error = {}, bool fatal = false);
    void scheduleReconnect();
    void dropClient();
    void adoptFingerprint(bool persist);
    void maybeUpgradeCredential(int epoch);
    void resetStores();
    void watchNetwork();

    // Events
    void onEvent(const QJsonObject &e);
    void onHello(const QJsonObject &e);
    void onMessage(const QString &chatId, Message m);
    void onTool(const QJsonObject &e);
    void onTurnEnd(const QJsonObject &e);
    void clearContinuing(ChatData &d);
    void upsertChat(const Chat &chat);
    void removeChat(const QString &chatId);
    void setChats(const QJsonArray &raw);
    void reconcileSelection();

    // Stores
    ChatData &data(const QString &chatId) { return m_data[chatId]; }
    bool isOpen(const QString &chatId) const { return chatId == m_selected; }
    void bindSelection();
    void markRead(const QString &chatId);
    void loadHistory(const QString &chatId);
    void trimHistory(const QString &chatId);
    void reapUnusedChats(const QString &keep = {});
    void refreshChats();
    void refreshModels(const QString &chatId = {});
    void refreshCommands();
    void turnChanged(const QString &chatId, bool shape);
    void hydrate();
    void scheduleSnapshot();
    void saveSnapshot();

    void reply(const QJSValue &cb, const QVariant &value) const;
    /// Run a chat command; a failure becomes a note in that chat.
    BridgeClient::Callback noteOnFailure(const QString &chatId, const QString &what);

    Store *m_store;
    QJSEngine *m_engine = nullptr;
    BridgeClient *m_client = nullptr;
    int m_epoch = 0;

    Conn m_conn = Idle;
    QString m_connError;
    QString m_connNote;
    bool m_connFatal = false;
    QTimer m_retry;
    int m_backoffMs = 800;
    qint64 m_fastRetryUntil = 0;
    int m_byeDelayMs = -1;
    qint64 m_lastLaunchMs = 0;
    QTimer m_networkDebounce;
    bool m_credentialsUnsupported = false;
    bool m_snapshots = true;
    bool m_retriedAuth = false;

    /// Which address a connect attempt uses: 0 is the configured one, n the
    /// profile's n-th usable fallback. It stays where a connection last
    /// worked, and moves on when one can't be reached.
    int m_endpointIndex = 0;
    /// While on a fallback: checks whether the configured address is back.
    QTimer m_failback;
    bool advanceEndpoint(const ApiResult &why);
    void probeConfigured();
    bool reloadCredential(const ApiResult &why);
    bool m_upgrading = false;

    QVariantMap m_status;
    ChatListModel m_chats;
    ChatFilterModel m_chatFilter;
    MessageListModel m_messages;
    TurnState m_turn;
    QString m_selected;
    std::map<QString, ChatData> m_data;
    QSet<QString> m_unusedChats;
    bool m_narrow = false;

    QVariantList m_models;
    QVariantList m_commands;
    /// The list is the client's own (see builtInCommands), not the daemon's.
    bool m_commandsBuiltIn = false;
    QStringList chatMarks(const char *kind) const;
    void setChatMark(const char *kind, const QString &chatId, bool on);
    QVariantMap m_daemonConfig;
    QVariantList m_devices;
    QVariantList m_locations;

    QHash<QString, QPointer<QNetworkReply>> m_uploads;
    int m_uploadSeq = 0;
    QTimer m_snapshotTimer;
};

} // namespace talos
