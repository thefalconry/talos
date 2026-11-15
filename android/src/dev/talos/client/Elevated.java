// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.app.Application;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.pm.ProviderInfo;
import android.net.Uri;
import android.os.Bundle;
import android.util.Log;

import org.json.JSONObject;

import java.io.FileInputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.Reader;
import java.lang.reflect.Method;
import java.nio.charset.StandardCharsets;

import moe.shizuku.api.BinderContainer;
import rikka.shizuku.Shizuku;
import rikka.shizuku.ShizukuProvider;

/**
 * Running a command as the shell user through Shizuku, for a person who has
 * installed and started it and allowed this app there.
 *
 * Shizuku hands its binder to the provider below, which lives in the app's
 * main process. The mesh can run in a process of its own, so that one asks
 * the provider for the binder and hears about a new one by broadcast.
 * Everything here answers "not available" when Shizuku is missing: the
 * caller then runs the command as the app.
 */
public final class Elevated {
    /** The provider Shizuku delivers its binder to, sharing it with the mesh's process. */
    public static class BinderProvider extends ShizukuProvider {
        @Override
        public void attachInfo(Context context, ProviderInfo info) {
            enableMultiProcessSupport(true);
            super.attachInfo(context, info);
        }
    }

    private static final String TAG = "TalosElevated";
    private static final String MANAGER = "moe.shizuku.privileged.api";
    private static final String EXTRA_BINDER = "moe.shizuku.privileged.api.intent.extra.BINDER";
    private static final int REQUEST = 4403;
    private static final long BINDER_WAIT_MS = 2000;
    private static final int HEAD = 192 * 1024;
    private static final int TAIL = 64 * 1024;

    private static final long RETRY_MS = 30_000;

    private static boolean started;
    private static boolean elsewhere;
    private static volatile long gaveUpAt;

    private Elevated() {}

    /**
     * Take the binder while the main process is there to give it: a mesh
     * process that asks only when a command arrives may find that process
     * gone, and a new one is not handed the binder until the app is opened.
     */
    public static void prepare(Context context) {
        final Context app = context.getApplicationContext();
        new Thread(() -> await(app, 0)).start();
    }

    private static synchronized void start(Context context) {
        if (started)
            return;
        started = true;
        try {
            elsewhere = !context.getPackageName().equals(Application.getProcessName());
            if (elsewhere) {
                ShizukuProvider.enableMultiProcessSupport(false);
                ShizukuProvider.requestBinderForNonProviderProcess(context.getApplicationContext());
            }
        } catch (Throwable t) {
            Log.d(TAG, "no binder to ask for: " + t);
        }
    }

    private static boolean alive() {
        try {
            return Shizuku.pingBinder();
        } catch (Throwable t) {
            return false;
        }
    }

    /** Ask the main process for the binder it holds; asking also starts that process. */
    private static void fetch(Context context) {
        if (!elsewhere)
            return;
        try {
            Bundle reply = context.getContentResolver().call(
                Uri.parse("content://" + context.getPackageName() + ".shizuku"),
                ShizukuProvider.METHOD_GET_BINDER, null, new Bundle());
            if (reply == null)
                return;
            reply.setClassLoader(BinderContainer.class.getClassLoader());
            BinderContainer container = reply.getParcelable(EXTRA_BINDER);
            if (container != null && container.binder != null && container.binder.pingBinder())
                Shizuku.onBinderReceived(container.binder, context.getPackageName());
        } catch (Throwable t) {
            Log.d(TAG, "binder not fetched: " + t);
        }
    }

    private static boolean await(Context context, long ms) {
        start(context);
        // Shizuku that was not there a moment ago is not waited for again
        // on every command.
        long now = System.currentTimeMillis();
        long deadline = now - gaveUpAt < RETRY_MS ? now : now + ms;
        while (true) {
            if (alive())
                return true;
            fetch(context);
            if (alive())
                return true;
            if (System.currentTimeMillis() >= deadline) {
                if (ms > 0)
                    gaveUpAt = System.currentTimeMillis();
                return false;
            }
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                return false;
            }
        }
    }

    private static boolean granted() {
        try {
            return !Shizuku.isPreV11()
                && Shizuku.checkSelfPermission() == PackageManager.PERMISSION_GRANTED;
        } catch (Throwable t) {
            return false;
        }
    }

    private static int uid() {
        try {
            return Shizuku.getUid();
        } catch (Throwable t) {
            return -1;
        }
    }

    /**
     * "absent" (Shizuku is not installed), "stopped" (installed, not
     * running), "ask" (running, this app not yet allowed), "denied" or
     * "ready"; "ready" is followed by the user id commands run as, 2000 for
     * the shell and 0 for root. With {@code wait}, a binder still on its way
     * is given a moment: call that off the main thread.
     */
    public static String status(Context context, boolean wait) {
        try {
            context.getPackageManager().getPackageInfo(MANAGER, 0);
        } catch (Throwable t) {
            return "absent";
        }
        if (!await(context, wait ? BINDER_WAIT_MS : 0))
            return "stopped";
        if (granted())
            return "ready " + uid();
        try {
            return Shizuku.shouldShowRequestPermissionRationale() ? "denied" : "ask";
        } catch (Throwable t) {
            return "stopped";
        }
    }

    /** Raise Shizuku's own question, or open Shizuku where there is nothing to ask. */
    public static void ask(Context context) {
        try {
            if (await(context, 0) && !granted() && !Shizuku.shouldShowRequestPermissionRationale()) {
                Shizuku.requestPermission(REQUEST);
                return;
            }
        } catch (Throwable t) {
            Log.d(TAG, "could not ask: " + t);
        }
        try {
            Intent open = context.getPackageManager().getLaunchIntentForPackage(MANAGER);
            if (open != null)
                context.startActivity(open.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
        } catch (Throwable t) {
            Log.d(TAG, "could not open Shizuku: " + t);
        }
    }

    /** What a stream printed, keeping its start and its end when it is long. */
    private static final class Output extends Thread {
        private final Reader in;
        private final StringBuilder head = new StringBuilder();
        private final StringBuilder tail = new StringBuilder();
        private long dropped;

        Output(InputStream stream) {
            in = new InputStreamReader(stream, StandardCharsets.UTF_8);
            start();
        }

        @Override
        public void run() {
            char[] buffer = new char[8192];
            try {
                int n;
                while ((n = in.read(buffer)) >= 0)
                    add(buffer, n);
            } catch (Throwable t) {
                // The process went away: what was read stands.
            }
        }

        private synchronized void add(char[] buffer, int n) {
            int toHead = Math.min(n, HEAD - head.length());
            head.append(buffer, 0, toHead);
            tail.append(buffer, toHead, n - toHead);
            if (tail.length() > 2 * TAIL) {
                dropped += tail.length() - TAIL;
                tail.delete(0, tail.length() - TAIL);
            }
        }

        synchronized String text() {
            int over = Math.max(0, tail.length() - TAIL);
            long gone = dropped + over;
            if (gone == 0)
                return head.toString() + tail;
            return head + "\n… [" + gone + " characters omitted] …\n" + tail.substring(over);
        }
    }

    /**
     * Run {@code cmd} through Shizuku and answer as JSON: {@code ran} false
     * with an {@code error} when Shizuku could not be used at all, otherwise
     * stdout, stderr, exitCode, timedOut and the uid it ran as. The file
     * {@code input}, when there is one, is what the command reads. Blocks.
     */
    public static String run(Context context, String cmd, String cwd, int timeoutMs,
                             String input) {
        JSONObject o = new JSONObject();
        try {
            if (!await(context, BINDER_WAIT_MS) || !granted()) {
                o.put("ran", false);
                o.put("error", "Shizuku is not running or has not allowed this app");
                return o.toString();
            }
            // The one call that starts a process is not part of the public
            // surface of this version of the library.
            Method start = null;
            for (Method m : Shizuku.class.getDeclaredMethods()) {
                Class<?>[] types = m.getParameterTypes();
                if (m.getName().equals("newProcess") && types.length == 3
                    && types[0] == String[].class)
                    start = m;
            }
            if (start == null)
                throw new NoSuchMethodException("Shizuku.newProcess");
            start.setAccessible(true);
            final Process process = (Process) start.invoke(
                null, new String[] {"sh", "-c", cmd}, null,
                cwd == null || cwd.isEmpty() ? null : cwd);
            if (input == null || input.isEmpty()) {
                process.getOutputStream().close();
            } else {
                new Thread(() -> {
                    try (InputStream from = new FileInputStream(input);
                         OutputStream to = process.getOutputStream()) {
                        byte[] buffer = new byte[65536];
                        int n;
                        while ((n = from.read(buffer)) >= 0)
                            to.write(buffer, 0, n);
                    } catch (Throwable t) {
                        Log.w(TAG, "input not delivered: " + t);
                    }
                }).start();
            }
            Output out = new Output(process.getInputStream());
            Output err = new Output(process.getErrorStream());
            // A remote process answers exitValue() with an exception while it
            // runs, so the wait is a thread with a deadline.
            Thread waiter = new Thread(() -> {
                try {
                    process.waitFor();
                } catch (Throwable t) {
                    // Interrupted, or the binder died: the join below times out.
                }
            });
            waiter.start();
            waiter.join(timeoutMs);
            boolean finished = !waiter.isAlive();
            int code = -1;
            if (finished)
                code = process.exitValue();
            else
                process.destroy();
            out.join(1000);
            err.join(1000);
            o.put("ran", true);
            o.put("stdout", out.text());
            o.put("stderr", err.text());
            o.put("exitCode", code);
            o.put("timedOut", !finished);
            o.put("uid", uid());
        } catch (Throwable t) {
            Log.w(TAG, "command not run through Shizuku", t);
            try {
                o.put("ran", false);
                o.put("error", String.valueOf(t.getCause() != null ? t.getCause() : t));
            } catch (Throwable ignored) {
                return "{\"ran\":false}";
            }
        }
        return o.toString();
    }
}
