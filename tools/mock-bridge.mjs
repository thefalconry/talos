#!/usr/bin/env node
// SPDX-License-Identifier: GPL-2.0-only
// A stand-in for a Talon daemon's client bridge (Bridge Protocol v1).
//
// It serves every route the client uses, with believable data and a canned
// streaming reply to anything you send, so the app can be developed, tested
// and screenshotted without a real daemon:
//
//   node tools/mock-bridge.mjs --port 19880 --token secret
//   node tools/mock-bridge.mjs --tls                # self-signed HTTPS
//   node tools/mock-bridge.mjs --tls --mtls         # also demands a client cert
//
// Options: --port N (0 picks one) · --token T · --tls · --mtls · --dir D
//          (where certificates go) · --discovery FILE (write a
//          native-bridge.json there, like the daemon does) · --slow MS ·
//          --no-commands (behave like a daemon that predates slash commands) ·
//          --endpoints JSON (fallback addresses for the register reply) ·
//          --busy (a reply's turn makes six tool calls, not one) ·
//          --essay (the reply is some 20 kB long)
//
// On start it prints one JSON line: {port, scheme, fingerprint, token, ...}.
// Test hooks (not part of the protocol): POST /__emit pushes any event to
// every stream, GET /__state returns what devices and clients have sent.

import http from "node:http";
import https from "node:https";
import { execFileSync } from "node:child_process";
import { createHash, randomBytes, X509Certificate } from "node:crypto";
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import zlib from "node:zlib";

const args = process.argv.slice(2);
const flag = (name) => args.includes(`--${name}`);
const opt = (name, fallback) => {
  const i = args.indexOf(`--${name}`);
  return i >= 0 && args[i + 1] ? args[i + 1] : fallback;
};

const token = opt("token", "");
const useTls = flag("tls") || flag("mtls");
const useMtls = flag("mtls");
const slow = Number(opt("slow", "35"));
// Fallback addresses to advertise in the register reply, as JSON:
// --endpoints '[{"url":"https://127.0.0.1:19881"}]'
const endpoints = JSON.parse(opt("endpoints", "null"));
// An older daemon: no `commands` capability, no GET /commands.
const noCommands = flag("no-commands");
// Only TLS needs somewhere to keep certificates; make it on demand.
const dir = opt("dir", "") || (flag("tls") || flag("mtls") ? fs.mkdtempSync(path.join(os.tmpdir(), "talon-mock-")) : "");
const startedAt = new Date().toISOString();

// ── State ───────────────────────────────────────────────────────────────────

let seq = 1000;
const nextId = () => String(++seq);
const now = () => Date.now();
const day = 86_400_000;

const chats = new Map();
const messages = new Map();
const streams = new Set();
const media = new Map();
const state = {
  commandResults: [],
  registrations: [],
  locations: [],
  sent: [],
  pairGrants: ["mock-grant"],
  uploads: [],
  secrets: [],
  transfers: [],
  credentialUpgrades: [],
};
const secretGrants = new Map([["live-grant", { name: "wifi-password" }]]);
const transfers = new Map();
let config = {
  backend: "claude",
  frontend: "native",
  model: "claude-sonnet-5",
  modelDisplay: "Claude Sonnet 5",
  botDisplayName: "Talon",
  timezone: "Europe/Dublin",
  pulse: true,
  pulseIntervalMs: 300000,
  heartbeat: true,
  heartbeatIntervalMinutes: 60,
  dream: true,
};
const editable = [
  "model", "botDisplayName", "timezone", "pulse", "pulseIntervalMs",
  "heartbeat", "heartbeatIntervalMinutes", "dream",
];
const plugins = [
  { name: "github", kind: "builtin", enabled: true, source: "config: github" },
  { name: "playwright", kind: "builtin", enabled: false, source: "config: playwright" },
  { name: "mempalace", kind: "builtin", enabled: true, source: "config: memory" },
  { name: "weather", kind: "mcp", enabled: true, source: "npx -y weather-mcp" },
];
const skills = [
  { name: "pdf", description: "Read, split, merge and fill PDF files", enabled: true },
  { name: "release-notes", description: "Draft release notes from merged pull requests", enabled: true },
  { name: "trip-planner", description: "Plan a trip from dates and a budget", enabled: false },
];
const models = [
  { id: "claude-fable-5-1", displayName: "Claude Fable 5.1", provider: "anthropic", reasoning: true },
  { id: "claude-opus-5-5", displayName: "Claude Opus 5.5", provider: "anthropic", reasoning: true },
  { id: "claude-sonnet-5", displayName: "Claude Sonnet 5", provider: "anthropic", reasoning: true },
  { id: "claude-haiku-5-5", displayName: "Claude Haiku 5.5", provider: "anthropic", reasoning: false },
];
const devices = new Map([
  ["dev_node01", {
    id: "dev_node01", name: "rack-01", platform: "linux", arch: "amd64", appVersion: "1.2.3",
    online: true, lastSeen: now(), capabilities: ["ring", "status", "exec", "read_file"],
  }],
  ["dev_pixel8", {
    id: "dev_pixel8", name: "Pixel 8", platform: "android", appVersion: "5.37.0",
    online: false, lastSeen: now() - 3 * 3600_000, battery: 62, charging: false,
    capabilities: ["locate", "ring", "status"],
  }],
]);
const locations = new Map([
  ["dev_pixel8", {
    deviceId: "dev_pixel8", lat: 53.3498, lon: -6.2603, accuracyM: 12.5,
    ts: now() - 3 * 3600_000, provider: "fused", batteryPct: 62,
  }],
]);
const memoryRows = [
  { id: 1, kind: "fact", subject: "user", text: "Prefers answers that lead with the conclusion.", trust: "stated", confidence: 0.95, pinned: true, hitCount: 14, salience: 0.9, createdAt: now() - 40 * day, lastSeenAt: now() - day },
  { id: 2, kind: "state", subject: "project/talos", key: "status", text: "Qt client: core done, UI in progress.", trust: "observed", confidence: 0.8, pinned: false, hitCount: 3, salience: 0.6, createdAt: now() - 2 * day, lastSeenAt: now() - 3600_000 },
  { id: 3, kind: "preference", subject: "user", text: "Works in Europe/Dublin; mornings are for deep work.", trust: "inferred", confidence: 0.6, pinned: false, hitCount: 5, salience: 0.5, createdAt: now() - 12 * day, lastSeenAt: now() - 2 * day },
];
const commands = [
  { name: "help", description: "All commands" },
  { name: "status", description: "Session info, context usage and stats" },
  { name: "model", description: "List models; pick one or switch backend", args: "[<n>|<id>|<backend>|default]" },
  { name: "effort", description: "Show or set thinking effort", args: "[off|low|medium|high|max|adaptive]" },
  { name: "stop", description: "Stop the current response" },
  { name: "reset", description: "Clear the session and start fresh" },
  { name: "memory", description: "What Talon remembers", args: "[<query>|why <id>]" },
  { name: "restart", description: "Restart the daemon", admin: true },
  { name: "secret", description: "Store a password via a one-time link", args: "<name>", admin: true },
];

function seedChat(id, title, ageMs, rows) {
  const chat = {
    id, title, createdAt: now() - ageMs - 60_000, lastActive: now() - ageMs,
    preview: "", model: "claude-sonnet-5", backend: "claude", effort: "adaptive", pulse: false,
    context: { known: true, used: 38200, max: 200000, pct: 19, warn: false },
  };
  chats.set(id, chat);
  const list = [];
  let ts = chat.createdAt;
  for (const [role, text, extra] of rows) {
    ts += 20_000;
    list.push({ id: nextId(), chatId: id, role, text, ts, ...(extra ?? {}) });
  }
  if (list.length) {
    list[list.length - 1].ts = chat.lastActive;
    chat.preview = preview(list[list.length - 1].text);
  }
  messages.set(id, list);
}

function preview(text) {
  const flat = text.replace(/[*_`#>]/g, "").replace(/\s+/g, " ").trim();
  return flat.length > 90 ? `${flat.slice(0, 89)}…` : flat;
}

// A real PNG, built here so the mock needs no binary asset: a soft
// two-colour gradient the image views can actually decode.
function makePng(width, height) {
  const crcTable = new Int32Array(256).map((_, n) => {
    let c = n;
    for (let k = 0; k < 8; k++) c = c & 1 ? 0xedb88320 ^ (c >>> 1) : c >>> 1;
    return c;
  });
  const crc = (buf) => {
    let c = -1;
    for (const b of buf) c = crcTable[(c ^ b) & 0xff] ^ (c >>> 8);
    return (c ^ -1) >>> 0;
  };
  const chunk = (type, data) => {
    const head = Buffer.alloc(8);
    head.writeUInt32BE(data.length, 0);
    head.write(type, 4, "ascii");
    const tail = Buffer.alloc(4);
    tail.writeUInt32BE(crc(Buffer.concat([head.subarray(4), data])), 0);
    return Buffer.concat([head, data, tail]);
  };
  const raw = Buffer.alloc(height * (1 + width * 3));
  for (let y = 0; y < height; y++) {
    const row = y * (1 + width * 3);
    for (let x = 0; x < width; x++) {
      const t = (x + y) / (width + height);
      raw[row + 1 + x * 3] = Math.round(194 + (58 - 194) * t);
      raw[row + 2 + x * 3] = Math.round(98 + (110 - 98) * t);
      raw[row + 3 + x * 3] = Math.round(45 + (165 - 45) * t);
    }
  }
  const header = Buffer.alloc(13);
  header.writeUInt32BE(width, 0);
  header.writeUInt32BE(height, 4);
  header.set([8, 2, 0, 0, 0], 8);
  return Buffer.concat([
    Buffer.from([0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a, 0x1a, 0x0a]),
    chunk("IHDR", header), chunk("IDAT", zlib.deflateSync(raw)), chunk("IEND", Buffer.alloc(0)),
  ]);
}
const pngPixel = makePng(480, 270);
media.set("m1", { bytes: pngPixel, type: "image/png", name: "diagram.png" });
media.set("m2", { bytes: Buffer.from("alpha,beta\n1,2\n"), type: "text/csv", name: "numbers.csv" });

seedChat("d_welcome", "Planning the Qt client", 90_000, [
  ["user", "What should the new client be built on?"],
  ["assistant",
    "**Qt Quick with a C++ core.** One codebase covers Linux, Windows, macOS and Android.\n\n" +
    "The split that keeps it testable:\n\n" +
    "1. A transport that only speaks the bridge protocol.\n" +
    "2. A session that owns chats, messages and the live turn.\n" +
    "3. QML that binds to both and holds no state of its own.\n\n" +
    "```cpp\nclass BridgeClient : public QObject {\n    Q_OBJECT\npublic:\n    void openEvents();   // one long-lived SSE stream\n    void post(const QString &path, const QJsonObject &body, Callback cb);\n};\n```\n\n" +
    "| Layer | Language | Knows about |\n|---|---|---|\n| Transport | C++ | HTTP, SSE, TLS pins |\n| Session | C++ | chats, turns |\n| Views | QML | nothing else |\n",
    {
      durationMs: 4200, tokensIn: 1200, tokensOut: 340,
      tools: [
        { id: "toolu_01", name: "Read", input: { file_path: "src/frontend/native/protocol.ts" }, durationMs: 40, output: "export const BRIDGE_PROTOCOL_VERSION = 1;" },
        { id: "toolu_02", name: "Bash", input: { command: "ls apps/", description: "See which clients exist" }, durationMs: 120, output: "companion\nnode" },
      ],
    }],
  ["user", "Here's the diagram I had in mind.", {
    imagePath: "/media?id=m1",
    attachments: [
      { path: "/home/user/.talon/workspace/uploads/diagram.png", name: "diagram.png", size: pngPixel.length, mimeType: "image/png", url: "/media?id=m1", image: true },
      { path: "/home/user/.talon/workspace/uploads/numbers.csv", name: "numbers.csv", size: 15, mimeType: "text/csv", url: "/media?id=m2", image: false },
    ],
  }],
  ["assistant", "That matches. The numbers file is small enough to read inline, so I did.", {
    buttons: [[{ text: "Open the protocol docs", url: "https://github.com/thefalconry/talon" }]],
    reactions: ["👍"],
  }],
]);
seedChat("d_trip", "Weekend in Galway", 26 * 3600_000, [
  ["user", "Two days in Galway, mostly on foot. Ideas?"],
  ["assistant", "Day one in the Latin Quarter and along the Claddagh; day two out to Salthill on the prom."],
]);
seedChat("d_bug", "Flaky reconnect on resume", 4 * day, [
  ["user", "The stream stays dead after the laptop wakes."],
  ["assistant", "A half-open socket never errors. Time out on silence instead: the daemon pings every 25 s."],
]);
seedChat("d_old", "Reading list", 45 * day, [
  ["user", "Remind me what I wanted to read."],
  ["assistant", "Three things: *The Pragmatic Programmer*, the Qt 6 QML book, and RFC 8439."],
]);

// ── Helpers ─────────────────────────────────────────────────────────────────

function status() {
  return {
    app: "talon-bridge", protocol: 1,
    capabilities: ["mesh", "mesh-commands", "plugins-skills", "attachments",
      ...(noCommands ? [] : ["commands"])],
    botName: config.botDisplayName, backend: config.backend, model: config.modelDisplay,
    activeChats: chats.size, startedAt,
  };
}

function chatList() {
  return [...chats.values()].sort((a, b) => b.lastActive - a.lastActive);
}

function broadcast(event, onlyDevice) {
  const frame = `data: ${JSON.stringify(event)}\n\n`;
  for (const s of streams) {
    if (onlyDevice && s.deviceId !== onlyDevice) continue;
    s.res.write(frame);
  }
}

function json(res, code, body) {
  const text = JSON.stringify(body);
  res.writeHead(code, { "Content-Type": "application/json", "Content-Length": Buffer.byteLength(text) });
  res.end(text);
}

function readBody(req) {
  return new Promise((resolve) => {
    const chunks = [];
    req.on("data", (c) => chunks.push(c));
    req.on("end", () => resolve(Buffer.concat(chunks)));
  });
}

async function readJson(req) {
  try {
    return JSON.parse((await readBody(req)).toString("utf8") || "{}");
  } catch {
    return {};
  }
}

const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function addMessage(chatId, role, text, extra = {}) {
  const chat = chats.get(chatId);
  if (!chat) return null;
  const m = { id: nextId(), chatId, role, text, ts: now(), ...extra };
  messages.get(chatId).push(m);
  chat.lastActive = m.ts;
  chat.preview = preview(text);
  broadcast({ kind: "message", chatId, message: m });
  broadcast({ kind: "chat_updated", chat });
  return m;
}

const running = new Map();

async function runTurn(chatId, userText) {
  const chat = chats.get(chatId);
  if (!chat) return;
  const turn = { cancelled: false };
  running.set(chatId, turn);
  broadcast({ kind: "turn_start", chatId });
  broadcast({ kind: "typing", chatId, on: true });
  const started = now();
  await sleep(slow * 6);
  for (const piece of ["The user wants a reply. ", "A short answer with one tool call shows the whole flow."]) {
    if (turn.cancelled) break;
    broadcast({ kind: "reasoning", chatId, text: piece });
    await sleep(slow * 4);
  }
  const toolId = `toolu_${nextId()}`;
  const clock = { command: "date -u +%H:%M", description: "Read the clock" };
  // --busy: a turn that works harder, to see a run of tool calls fold up.
  const extra = !flag("busy") ? [] : [
    { name: "Read", input: { file_path: "src/core/Session.cpp" }, output: "// 2279 lines" },
    { name: "Bash", input: { command: "cmake --build build", description: "Build the app" }, output: "[170/170] Linking" },
    { name: "Grep", input: { pattern: "reconnect", path: "src" }, output: "src/core/Session.cpp:127" },
    { name: "Bash", input: { command: "ctest --test-dir build", description: "Run the tests" }, output: "100% tests passed" },
    { name: "mcp__mail__send_email", input: { to: "ada@example.org" }, output: "sent" },
  ].map((t) => ({ ...t, id: `toolu_${nextId()}`, durationMs: slow * 8 }));
  if (!turn.cancelled) {
    broadcast({ kind: "tool", chatId, id: toolId, name: "Bash", phase: "call", input: clock });
    await sleep(slow * 10);
    broadcast({ kind: "tool", chatId, id: toolId, name: "Bash", phase: "result", output: new Date().toISOString().slice(11, 16) });
  }
  for (const t of extra) {
    if (turn.cancelled) break;
    broadcast({ kind: "tool", chatId, id: t.id, name: t.name, phase: "call", input: t.input });
    await sleep(t.durationMs);
    broadcast({ kind: "tool", chatId, id: t.id, name: t.name, phase: "result", output: t.output });
  }
  // --essay: a long reply, to see how the client keeps up with a stream.
  const essay = !flag("essay") ? "" : Array.from({ length: 60 }, (_, i) =>
    `### Part ${i + 1}\n\nThe quick brown fox jumps over the lazy dog, and then **does it again** ` +
    `with \`code\` in the middle, a [link](https://example.org/${i}) and enough words to wrap ` +
    `across a few lines of the column so that layout has real work to do each time.\n\n` +
    `- first point of part ${i + 1}\n- second point, a little longer than the first\n\n`).join("") +
    "```cpp\nint main() { return 0; }\n```\n\n";
  const answer = essay +
    `You said: *${userText.trim() || "(a file)"}*\n\n` +
    "This is the mock bridge answering, token by token. A real daemon would run a model here.\n\n" +
    "```python\ndef greet(name: str) -> str:\n    return f\"Hello, {name}!\"  # streamed code\n```\n\n" +
    "Done.";
  let sent = "";
  for (const word of answer.split(/(?<=\s)/)) {
    if (turn.cancelled) break;
    sent += word;
    broadcast({ kind: "delta", chatId, text: word });
    await sleep(slow);
  }
  if (sent) {
    addMessage(chatId, "assistant", turn.cancelled ? `${sent.trim()} …` : answer, {
      tools: [{ id: toolId, name: "Bash", input: clock, durationMs: slow * 10, output: new Date().toISOString().slice(11, 16) }, ...extra],
    });
  }
  chat.context = { known: true, used: chat.context.used + 2100, max: 200000, pct: Math.round((chat.context.used + 2100) / 2000), warn: false };
  broadcast({ kind: "turn_end", chatId, delivered: sent ? 1 : 0, durationMs: now() - started, usage: { input: 1840, output: 96 } });
  broadcast({ kind: "typing", chatId, on: false });
  running.delete(chatId);
  if (chat.queued) {
    const text = chat.queued.text;
    delete chat.queued;
    broadcast({ kind: "chat_updated", chat });
    addMessage(chatId, "user", text);
    runTurn(chatId, text);
  } else {
    broadcast({ kind: "chat_updated", chat });
  }
}

function authorized(req, url) {
  if (!token) return true;
  const header = req.headers.authorization ?? "";
  const bearer = header.startsWith("Bearer ") ? header.slice(7) : "";
  const query = url.pathname === "/events" || url.pathname === "/media" ? url.searchParams.get("token") : null;
  const presented = bearer || query || "";
  return presented === token || state.credentialUpgrades.some((c) => c.token === presented);
}

const PUBLIC = new Set(["/health", "/pair", "/secret", "/__emit", "/__state"]);

// ── Routes ──────────────────────────────────────────────────────────────────

async function handle(req, res) {
  const url = new URL(req.url, "http://mock");
  const route = `${req.method} ${url.pathname}`;
  const authed = authorized(req, url);

  if (route === "GET /health") {
    const base = {
      app: "talon-bridge", ok: true, protocol: 1, port: server.address().port,
      scheme: useTls ? "https" : "http", fingerprint: fingerprint || null, authRequired: Boolean(token),
    };
    return json(res, 200, authed && token ? { ...base, ...status() } : base);
  }
  if (!PUBLIC.has(url.pathname) && !authed) return json(res, 401, { ok: false, error: "Unauthorized" });

  switch (route) {
    case "GET /events": {
      res.writeHead(200, {
        "Content-Type": "text/event-stream", "Cache-Control": "no-cache, no-transform",
        Connection: "keep-alive", "X-Accel-Buffering": "no",
      });
      res.write("retry: 3000\n\n");
      const stream = { res, deviceId: url.searchParams.get("deviceId") || undefined };
      streams.add(stream);
      res.write(`data: ${JSON.stringify({ kind: "hello", status: status(), chats: chatList() })}\n\n`);
      req.on("close", () => streams.delete(stream));
      return;
    }
    case "GET /chats": return json(res, 200, { chats: chatList() });
    case "POST /chats": {
      const body = await readJson(req);
      const id = `d_${randomBytes(4).toString("hex")}`;
      const chat = {
        id, title: body.title || "New chat", createdAt: now(), lastActive: now(), preview: "",
        model: config.model, backend: config.backend, effort: "adaptive", pulse: false,
        context: { known: true, used: 0, max: 200000, pct: 0, warn: false },
      };
      chats.set(id, chat);
      messages.set(id, []);
      broadcast({ kind: "chat_created", chat });
      return json(res, 200, { chat });
    }
    case "POST /chats/rename": {
      const body = await readJson(req);
      const chat = chats.get(body.chatId);
      if (!chat) return json(res, 404, { ok: false, error: "No such chat" });
      chat.title = body.title;
      broadcast({ kind: "chat_updated", chat });
      return json(res, 200, { chat });
    }
    case "POST /chats/delete": {
      const body = await readJson(req);
      const ok = chats.delete(body.chatId);
      messages.delete(body.chatId);
      if (ok) broadcast({ kind: "chat_deleted", chatId: body.chatId });
      return json(res, 200, { ok });
    }
    case "POST /chats/reset": {
      const body = await readJson(req);
      const chat = chats.get(body.chatId);
      if (chat) {
        messages.set(body.chatId, []);
        chat.preview = "";
        chat.context = { known: true, used: 0, max: 200000, pct: 0, warn: false };
        broadcast({ kind: "chat_updated", chat });
      }
      return json(res, 200, { ok: Boolean(chat) });
    }
    case "POST /chats/interrupt": {
      const body = await readJson(req);
      const turn = running.get(body.chatId);
      if (turn) turn.cancelled = true;
      return json(res, 200, { ok: Boolean(turn) });
    }
    case "POST /chats/pulse": {
      const body = await readJson(req);
      const chat = chats.get(body.chatId);
      if (chat) {
        chat.pulse = body.on === true;
        broadcast({ kind: "chat_updated", chat });
      }
      return json(res, 200, { ok: true });
    }
    case "POST /queue": {
      const body = await readJson(req);
      const chat = chats.get(body.chatId);
      if (chat) {
        if (body.text) chat.queued = { text: body.text, hasAttachment: false };
        else delete chat.queued;
        broadcast({ kind: "chat_updated", chat });
      }
      return json(res, 200, { ok: true });
    }
    case "GET /history": {
      const id = url.searchParams.get("chatId") ?? "";
      const before = Number(url.searchParams.get("before") || 0);
      const limit = Number(url.searchParams.get("limit") || 50);
      let list = messages.get(id) ?? [];
      if (before) list = list.filter((m) => Number(m.id) < before);
      return json(res, 200, { chatId: id, messages: list.slice(-limit) });
    }
    case "GET /search": {
      const q = (url.searchParams.get("q") ?? "").toLowerCase();
      if (!q) return json(res, 400, { ok: false, error: "q required" });
      const only = url.searchParams.get("chatId");
      const results = [];
      for (const [chatId, list] of messages) {
        if (only && chatId !== only) continue;
        for (const m of list) {
          if (m.text.toLowerCase().includes(q)) {
            results.push({ chatId, chatTitle: chats.get(chatId)?.title ?? "", message: m });
          }
        }
      }
      return json(res, 200, { results });
    }
    case "POST /send": {
      const body = await readJson(req);
      const chat = chats.get(body.chatId);
      const refs = Array.isArray(body.attachments) ? body.attachments : [];
      if (!chat || (!String(body.text ?? "").trim() && refs.length === 0)) {
        return json(res, 400, { ok: false, error: "chatId and text (or an attachment) required" });
      }
      state.sent.push(body);
      if (running.has(body.chatId)) {
        chat.queued = { text: body.text, hasAttachment: refs.length > 0, attachmentCount: refs.length };
        broadcast({ kind: "chat_updated", chat });
        return json(res, 202, { ok: true });
      }
      const attachments = refs
        .map((r) => state.uploads.find((u) => u.url === r.url || u.path === r.path))
        .filter(Boolean);
      const first = attachments.find((a) => a.image);
      addMessage(body.chatId, "user", body.text ?? "", {
        ...(attachments.length ? { attachments } : {}),
        ...(first ? { imagePath: first.url } : {}),
      });
      if (chat.title === "New chat" && body.text) {
        chat.title = body.text.trim().slice(0, 40);
        broadcast({ kind: "chat_updated", chat });
      }
      runTurn(body.chatId, body.text ?? "");
      return json(res, 202, { ok: true });
    }
    case "GET /pair": {
      // Single use, like the daemon's: serving it is the handover.
      const grant = url.searchParams.get("grant") ?? "";
      if (!state.pairGrants.includes(grant))
        return json(res, 404, { ok: false, error: "Unknown, expired, or already-used pairing link" });
      state.pairGrants = state.pairGrants.filter((g) => g !== grant);
      const payload = {
        url: `${useTls ? "https" : "http"}://127.0.0.1:${server.address().port}`, token,
        ...(fingerprint ? { fingerprint } : {}), label: "Mock bridge",
      };
      if (url.searchParams.get("format") === "json") return json(res, 200, payload);
      const q = new URLSearchParams({ u: payload.url, t: payload.token });
      if (payload.fingerprint) q.set("f", payload.fingerprint);
      q.set("n", payload.label);
      res.writeHead(200, { "Content-Type": "text/html; charset=utf-8" });
      return res.end(`<!doctype html><a href="talon://pair?${q}">Open in Talon</a>`);
    }
    case "GET /commands":
      return noCommands ? json(res, 404, { ok: false, error: "Not found" })
                        : json(res, 200, { commands });
    case "POST /upload": {
      const bytes = await readBody(req);
      if (bytes.length === 0) return json(res, 400, { ok: false, error: "Upload was empty" });
      const name = url.searchParams.get("filename") ?? "upload";
      const type = req.headers["content-type"] ?? "application/octet-stream";
      const id = `m${nextId()}`;
      media.set(id, { bytes, type, name });
      const attachment = {
        path: `/home/user/.talon/workspace/uploads/${id}-${name}`, name, size: bytes.length,
        mimeType: type, url: `/media?id=${id}`, image: type.startsWith("image/"),
      };
      state.uploads.push({ ...attachment, sha256: createHash("sha256").update(bytes).digest("hex") });
      return json(res, 200, { ok: true, ...attachment, imagePath: attachment.url });
    }
    case "GET /media": {
      const item = media.get(url.searchParams.get("id") ?? "");
      if (!item) return json(res, 404, { ok: false, error: "No such media" });
      res.writeHead(200, {
        "Content-Type": item.type, "Content-Length": item.bytes.length,
        "Content-Disposition": `inline; filename="${item.name}"`,
      });
      return res.end(item.bytes);
    }
    case "GET /models": {
      const chat = chats.get(url.searchParams.get("chatId") ?? "");
      return json(res, 200, { active: chat?.model ?? config.model, models });
    }
    case "POST /model": {
      const body = await readJson(req);
      const chat = chats.get(body.chatId);
      if (chat) {
        chat.model = body.model;
        broadcast({ kind: "chat_updated", chat });
      }
      return json(res, 200, { ok: true });
    }
    case "GET /backends": {
      const chat = chats.get(url.searchParams.get("chatId") ?? "");
      return json(res, 200, {
        active: chat?.backend ?? config.backend,
        backends: [{ id: "claude", label: "Claude SDK" }, { id: "codex", label: "Codex" }, { id: "kilo", label: "Kilo" }],
      });
    }
    case "POST /backend": {
      const body = await readJson(req);
      const chat = chats.get(body.chatId);
      if (!chat) return json(res, 200, { ok: false, error: "No such chat" });
      if (body.backend === "kilo") return json(res, 200, { ok: false, error: "Backend not available" });
      chat.backend = body.backend;
      broadcast({ kind: "chat_updated", chat });
      return json(res, 200, { ok: true });
    }
    case "GET /effort": {
      const chat = chats.get(url.searchParams.get("chatId") ?? "");
      return json(res, 200, { active: chat?.effort ?? "adaptive", levels: ["adaptive", "off", "low", "medium", "high", "max"] });
    }
    case "POST /effort": {
      const body = await readJson(req);
      const chat = chats.get(body.chatId);
      if (chat) {
        chat.effort = body.effort;
        broadcast({ kind: "chat_updated", chat });
      }
      return json(res, 200, { ok: true });
    }
    case "GET /config":
    case "POST /config": {
      if (req.method === "POST") {
        const body = await readJson(req);
        for (const key of editable) if (key in body) config[key] = body[key];
        config.modelDisplay = models.find((m) => m.id === config.model)?.displayName ?? config.model;
        broadcast({ kind: "status", status: status() });
      }
      return json(res, 200, {
        ...config, editable,
        health: { healthy: true, uptimeMs: now() - Date.parse(startedAt) + 5 * 3600_000, sessions: chats.size, messages: 1284, memoryMb: 212 },
      });
    }
    case "POST /control": {
      const body = await readJson(req);
      if (body.action === "restart") {
        setTimeout(() => {
          broadcast({ kind: "bye", reason: "restart", reconnectAfterMs: 400 });
          for (const s of streams) s.res.end();
        }, 150);
        return json(res, 200, { ok: true, message: "Restarting Talon — back online in a few seconds." });
      }
      if (body.action === "dream") return json(res, 200, { ok: true, message: "Dream started — consolidating memory." });
      return json(res, 200, { ok: false, message: `Unknown control action: ${body.action}` });
    }
    case "GET /plugins": return json(res, 200, { plugins });
    case "POST /plugins/toggle": {
      const body = await readJson(req);
      const item = plugins.find((p) => p.name === body.name);
      if (!item) return json(res, 200, { ok: false, error: "No such plugin" });
      if (item.name === "playwright" && body.enabled) return json(res, 200, { ok: false, error: "Chromium is not installed on the host" });
      item.enabled = body.enabled === true;
      return json(res, 200, { ok: true });
    }
    case "GET /skills": return json(res, 200, { skills });
    case "POST /skills/toggle": {
      const body = await readJson(req);
      const item = skills.find((s) => s.name === body.name);
      if (!item) return json(res, 200, { ok: false, error: "No such skill" });
      item.enabled = body.enabled === true;
      return json(res, 200, { ok: true });
    }
    case "GET /logs": {
      const order = ["trace", "debug", "info", "warn", "error", "fatal"];
      const min = order.indexOf(url.searchParams.get("level") ?? "trace");
      const component = url.searchParams.get("component");
      const all = [
        { ts: now() - 60_000, level: "info", component: "native", msg: "Client bridge listening on 0.0.0.0:19880" },
        { ts: now() - 50_000, level: "debug", component: "agent", msg: "[d_welcome] turn started" },
        { ts: now() - 42_000, level: "warn", component: "mesh", msg: "Device dev_pixel8 missed 3 heartbeats" },
        { ts: now() - 30_000, level: "error", component: "agent", msg: "[d_bug] SDK error: transient stream drop", err: "socket hang up", stack: "Error: socket hang up\n    at runChatTurn (turn.ts:212)\n    at dispatch (dispatcher.ts:88)" },
        { ts: now() - 5_000, level: "info", component: "gateway", msg: "Health check ok" },
      ];
      return json(res, 200, {
        entries: all.filter((e) => order.indexOf(e.level) >= Math.max(min, 0) && (!component || e.component === component)),
      });
    }
    case "GET /memory": {
      const q = (url.searchParams.get("q") ?? "").toLowerCase();
      const kind = url.searchParams.get("kind");
      if (kind && !["fact", "state", "preference"].includes(kind)) {
        return json(res, 400, { ok: false, error: `Unknown kind "${kind}" (expected one of fact, state, preference)` });
      }
      return json(res, 200, {
        rows: memoryRows.filter((r) => (!kind || r.kind === kind) && (!q || r.text.toLowerCase().includes(q))),
      });
    }
    case "GET /memory/why": {
      const row = memoryRows.find((r) => String(r.id) === url.searchParams.get("id"));
      if (!row) return json(res, 404, { ok: false, error: `No memory with id ${url.searchParams.get("id")}` });
      return json(res, 200, {
        row,
        history: [
          { op: "assert", at: row.createdAt, reason: "Said in conversation" },
          { op: "pin", at: row.createdAt + day, reason: "Referenced repeatedly" },
        ],
      });
    }
    case "GET /devices": return json(res, 200, { devices: [...devices.values()], locations: [...locations.values()] });
    case "POST /devices/register": {
      const body = await readJson(req);
      if (!body.id) return json(res, 400, { ok: false, error: "id required" });
      state.registrations.push(body);
      devices.set(body.id, { ...body, online: true, lastSeen: now() });
      return json(res, 200, {
        ok: true, deviceId: body.id,
        ...(endpoints ? {
          endpoints: {
            v: createHash("sha256").update(JSON.stringify(endpoints)).digest("hex"),
            list: endpoints,
          },
        } : {}),
      });
    }
    case "POST /location": {
      const body = await readJson(req);
      state.locations.push(body);
      if (body.deviceId) locations.set(body.deviceId, body);
      return json(res, 200, { ok: true });
    }
    case "POST /devices/command-result": {
      const body = await readJson(req);
      state.commandResults.push(body);
      return json(res, 200, { ok: true });
    }
    case "POST /devices/file": {
      const t = transfers.get(url.searchParams.get("transfer") ?? "");
      const bytes = await readBody(req);
      if (!t) return json(res, 409, { ok: false, error: "Unknown or already-used transfer token" });
      state.transfers.push({ direction: "up", bytes: bytes.length, sha256: createHash("sha256").update(bytes).digest("hex"), deviceId: url.searchParams.get("deviceId") });
      return json(res, 200, { ok: true });
    }
    case "GET /devices/file": {
      const t = transfers.get(url.searchParams.get("transfer") ?? "");
      if (!t) return json(res, 404, { ok: false, error: "Unknown or already-used transfer token" });
      res.writeHead(200, { "Content-Type": "application/octet-stream", "Content-Length": t.body.length });
      return res.end(t.body);
    }
    case "GET /auth/whoami": {
      const presented = (req.headers.authorization ?? "").slice(7);
      const own = state.credentialUpgrades.find((c) => c.token === presented);
      if (own) return json(res, 200, { ok: true, kind: "device", scopes: own.scopes, credentialId: own.credentialId, deviceId: own.deviceId });
      if (!token) return json(res, 200, { ok: true, kind: "open", scopes: ["device", "client", "operator"] });
      return json(res, 200, { ok: true, kind: "shared", scopes: ["device", "client", "operator"], action: "upgrade" });
    }
    case "POST /auth/upgrade": {
      const body = await readJson(req);
      if (!body.deviceId) return json(res, 400, { ok: false, error: "deviceId is required (1-128 of A-Z a-z 0-9 . _ : @ -)" });
      const credentialId = randomBytes(8).toString("hex");
      const grant = {
        ok: true, token: `tdc1.${credentialId}.${randomBytes(32).toString("base64url")}`,
        credentialId, deviceId: body.deviceId, scopes: body.scopes ?? ["device", "client"],
      };
      state.credentialUpgrades.push(grant);
      return json(res, 200, grant);
    }
    case "GET /secret":
    case "POST /secret": {
      const grant = secretGrants.get(url.searchParams.get("grant") ?? "");
      const page = (body) => {
        res.writeHead(grant ? 200 : 404, { "Content-Type": "text/html; charset=utf-8" });
        res.end(`<!doctype html><html><body><h1>${body}</h1></body></html>`);
      };
      if (!grant) return page("This link has expired");
      if (req.method === "GET") return page(`Store <b>${grant.name}</b>`);
      const value = (await readBody(req)).toString("utf8");
      secretGrants.delete(url.searchParams.get("grant"));
      state.secrets.push({ name: grant.name, length: value.length, sha256: createHash("sha256").update(value).digest("hex") });
      return page(`Stored as ${grant.name}`);
    }
    case "GET /download": {
      // A stand-in for any file the bridge serves on its own address.
      const body = Buffer.from("talon mock download\n".repeat(64));
      res.writeHead(200, {
        "Content-Type": "application/octet-stream", "Content-Length": body.length,
        "Content-Disposition": 'attachment; filename="talon-report.txt"',
      });
      return res.end(body);
    }
    case "POST /__emit": {
      const body = await readJson(req);
      for (const event of Array.isArray(body) ? body : [body]) {
        if (event.__transfer) transfers.set(event.__transfer.token, { body: Buffer.from(event.__transfer.body ?? "", "utf8") });
        else broadcast(event, event.kind === "device_command" ? undefined : undefined);
      }
      return json(res, 200, { ok: true, streams: streams.size });
    }
    case "GET /__state":
      return json(res, 200, {
        ...state, chats: [...chats.values()],
        streams: [...streams].map((s) => s.deviceId ?? null),
      });
    default:
      return json(res, 404, { ok: false, error: `No route ${route}` });
  }
}

// ── TLS material ────────────────────────────────────────────────────────────

let fingerprint = "";
let tlsOptions = null;
if (useTls) {
  const run = (argv) => execFileSync("openssl", argv, { cwd: dir, stdio: "pipe" });
  // The curve by name: LibreSSL (macOS's openssl) would otherwise spell out
  // its parameters, and OpenSSL refuses a certificate whose key does that.
  const ecKey = ["-newkey", "ec", "-pkeyopt", "ec_paramgen_curve:prime256v1",
    "-pkeyopt", "ec_param_enc:named_curve", "-nodes"];
  if (!fs.existsSync(path.join(dir, "bridge.crt"))) {
    run(["req", "-x509", ...ecKey,
      "-keyout", "bridge.key", "-out", "bridge.crt", "-days", "30", "-subj", "/CN=talon-mock-bridge"]);
  }
  tlsOptions = { key: fs.readFileSync(path.join(dir, "bridge.key")), cert: fs.readFileSync(path.join(dir, "bridge.crt")) };
  fingerprint = new X509Certificate(tlsOptions.cert).fingerprint256.replace(/:/g, "").toLowerCase();
  if (useMtls) {
    if (!fs.existsSync(path.join(dir, "client.p12"))) {
      run(["req", "-x509", ...ecKey,
        "-keyout", "ca.key", "-out", "ca.crt", "-days", "30", "-subj", "/CN=Taloss"]);
      run(["req", ...ecKey,
        "-keyout", "client.key", "-out", "client.csr", "-subj", "/CN=test-device"]);
      run(["x509", "-req", "-in", "client.csr", "-CA", "ca.crt", "-CAkey", "ca.key", "-CAcreateserial",
        "-days", "30", "-sha256", "-out", "client.crt"]);
      run(["pkcs12", "-export", "-in", "client.crt", "-inkey", "client.key", "-out", "client.p12",
        "-passout", "pass:mock-p12-password"]);
    }
    tlsOptions.ca = fs.readFileSync(path.join(dir, "ca.crt"));
    tlsOptions.requestCert = true;
    tlsOptions.rejectUnauthorized = true;
  }
}

const listener = (req, res) => {
  handle(req, res).catch((err) => json(res, 500, { ok: false, error: String(err) }));
};
const server = useTls ? https.createServer(tlsOptions, listener) : http.createServer(listener);
const keepAlive = setInterval(() => {
  for (const s of streams) s.res.write(": ping\n\n");
}, 25_000);
keepAlive.unref();

server.listen(Number(opt("port", "19880")), "127.0.0.1", () => {
  const info = {
    port: server.address().port, scheme: useTls ? "https" : "http", fingerprint,
    token, mtls: useMtls, dir, p12: useMtls ? path.join(dir, "client.p12") : "",
    p12Password: useMtls ? "mock-p12-password" : "",
  };
  const discovery = opt("discovery", "");
  if (discovery) {
    fs.mkdirSync(path.dirname(discovery), { recursive: true });
    fs.writeFileSync(discovery, JSON.stringify({
      host: "127.0.0.1", port: info.port, token: token || null, scheme: info.scheme,
      fingerprint: fingerprint || null, pid: process.pid, protocol: 1, startedAt: now(), updatedAt: now(),
    }, null, 2), { mode: 0o600 });
  }
  console.log(JSON.stringify(info));
});
