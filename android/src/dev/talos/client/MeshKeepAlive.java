// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.os.Build;
import android.os.IBinder;

/**
 * Keeps the app's process in the foreground class while the mesh is on, so
 * Android doesn't freeze its connection the moment the window is left.
 *
 * It runs no code of its own: the connection lives in the app's process and
 * this only tells the system, with a notification the user can see, that the
 * process is still doing something for them. It does not survive the app
 * being swiped out of the recent-apps list, because that ends the process.
 */
public class MeshKeepAlive extends Service {
    private static final String CHANNEL = "talos.mesh";
    private static final int NOTIFICATION = 7001;

    /** Start or stop the service; called from the app's own process. */
    public static void set(Context context, boolean on, String text) {
        Intent intent = new Intent(context, MeshKeepAlive.class);
        if (!on) {
            context.stopService(intent);
            return;
        }
        intent.putExtra("text", text);
        try {
            context.startForegroundService(intent);
        } catch (RuntimeException e) {
            // Refused when the app is not in the foreground itself; the next
            // time the window is opened it is asked for again.
        }
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        String text = intent != null ? intent.getStringExtra("text") : null;
        NotificationManager manager =
            (NotificationManager) getSystemService(Context.NOTIFICATION_SERVICE);
        manager.createNotificationChannel(new NotificationChannel(
            CHANNEL, "Mesh connection", NotificationManager.IMPORTANCE_LOW));

        Intent open = getPackageManager().getLaunchIntentForPackage(getPackageName());
        PendingIntent tap = open == null ? null : PendingIntent.getActivity(
            this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification notification = new Notification.Builder(this, CHANNEL)
            .setSmallIcon(android.R.drawable.stat_notify_sync_noanim)
            .setContentTitle("Talos")
            .setContentText(text == null || text.isEmpty() ? "Connected to the mesh" : text)
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
        // Not restarted on its own: with the process gone there is no
        // connection for it to keep.
        return START_NOT_STICKY;
    }

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }
}
