// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.content.Context;
import android.content.Intent;

/**
 * The app's notifications, from the window's process or the background
 * service's alike. A reply's notification belongs to its chat: a newer
 * reply there takes its place, and tapping it opens that chat.
 */
public final class Notify {
    private static final String CHANNEL = "talon.messages";
    static final int ACCENT = 0xFFE07A45;
    private static int next = 1;

    private Notify() {}

    /** The talon mark; the package's resources go by its id, which can vary. */
    static int smallIcon(Context context) {
        int icon = context.getResources().getIdentifier(
            "ic_stat_talos", "drawable", context.getPackageName());
        return icon != 0 ? icon : android.R.drawable.stat_notify_chat;
    }

    /** The same number for a chat in either process, clear of the others. */
    private static int idFor(String chatId) {
        return 0x10000 + (chatId.hashCode() & 0x7FFFFF);
    }

    public static synchronized void post(Context context, String title, String body,
                                         String chatId) {
        NotificationManager manager =
            (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager == null)
            return;
        // Creating a channel that already exists changes nothing.
        manager.createNotificationChannel(new NotificationChannel(
            CHANNEL, "Messages", NotificationManager.IMPORTANCE_DEFAULT));
        boolean forChat = chatId != null && !chatId.isEmpty();
        int id = forChat ? idFor(chatId) : next++;
        // Addressed to the activity itself, not as the launcher's intent:
        // that one only brings a running app forward and delivers nothing.
        Intent open = new Intent(context, TalosActivity.class)
            .setFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        if (forChat)
            open.putExtra(TalosActivity.EXTRA_CHAT, chatId);
        PendingIntent tap = PendingIntent.getActivity(
            context, id, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        Notification.Builder builder = new Notification.Builder(context, CHANNEL)
            .setSmallIcon(smallIcon(context))
            .setColor(ACCENT)
            .setContentTitle(title)
            .setContentText(body)
            .setStyle(new Notification.BigTextStyle().bigText(body))
            .setAutoCancel(true)
            .setContentIntent(tap);
        if (forChat)
            builder.setCategory(Notification.CATEGORY_MESSAGE);
        manager.notify(id, builder.build());
    }

    /** The chat is on screen: what was said about it has been seen. */
    public static void cancel(Context context, String chatId) {
        NotificationManager manager =
            (NotificationManager) context.getSystemService(Context.NOTIFICATION_SERVICE);
        if (manager != null && chatId != null && !chatId.isEmpty())
            manager.cancel(idFor(chatId));
    }
}
