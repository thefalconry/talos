// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;
import android.content.SharedPreferences;
import android.content.pm.ServiceInfo;
import android.os.Build;

import org.qtproject.qt.android.bindings.QtService;

/**
 * The connection to Talon, running without the app's window.
 *
 * It is the same program started with `--headless`, in a process of its own
 * (see the manifest), so it goes on after the window is closed or swiped
 * away and can be started when the phone boots. Android requires a service
 * like this to show a notification for as long as it runs.
 */
public class MeshHost extends QtService {
    private static final String CHANNEL = "talos.mesh";
    private static final int NOTIFICATION = 7001;
    private static final String PREFS = "talos";
    private static final String WANTED = "mesh_background";

    /** Whether the user asked for the background connection. */
    public static boolean wanted(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(WANTED, false);
    }

    /** Turn the background connection on or off; called from the app. */
    public static void set(Context context, boolean on) {
        SharedPreferences prefs = context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        prefs.edit().putBoolean(WANTED, on).apply();
        Intent intent = new Intent(context, MeshHost.class);
        if (!on) {
            context.stopService(intent);
            return;
        }
        start(context);
    }

    static void start(Context context) {
        try {
            context.startForegroundService(new Intent(context, MeshHost.class));
        } catch (RuntimeException e) {
            // Not allowed right now (the app isn't in front); it is asked
            // for again the next time the window opens.
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        NotificationManager manager =
            (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        manager.createNotificationChannel(new NotificationChannel(
            CHANNEL, "Background connection", NotificationManager.IMPORTANCE_LOW));
        Intent open = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent tap = open == null ? null : PendingIntent.getActivity(
            this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notification = new Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
            .setContentTitle("Talos")
            .setContentText("Connected to Talon in the background")
            .setContentIntent(tap)
            .setOngoing(true)
            .build();
        try {
            if (Build.VERSION.SDK_INT >= 34) {
                startForeground(NOTIFICATION, notification,
                                ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
            } else {
                startForeground(NOTIFICATION, notification);
            }
        } catch (RuntimeException e) {
            stopSelf();
            return START_NOT_STICKY;
        }
        super.onStartCommand(intent, flags, startId);
        // Brought back if the system has to reclaim the process.
        return START_STICKY;
    }
}
