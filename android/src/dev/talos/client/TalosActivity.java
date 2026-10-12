// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;

import org.json.JSONArray;
import org.json.JSONObject;
import org.qtproject.qt.android.bindings.QtActivity;

import java.util.ArrayList;

/**
 * The app's window, plus the two things Qt's own activity doesn't do: take
 * what another app shares ("Share to Talos"), and ask for the permission to
 * post notifications.
 */
public class TalosActivity extends QtActivity {
    private static String pendingShare;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        capture(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        capture(intent);
    }

    /** Remember a share until the app, which may still be starting, asks for it. */
    private static synchronized void capture(Intent intent) {
        if (intent == null)
            return;
        String action = intent.getAction();
        if (!Intent.ACTION_SEND.equals(action) && !Intent.ACTION_SEND_MULTIPLE.equals(action))
            return;
        try {
            JSONObject share = new JSONObject();
            CharSequence text = intent.getCharSequenceExtra(Intent.EXTRA_TEXT);
            share.put("text", text == null ? "" : text.toString());
            JSONArray files = new JSONArray();
            if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
                ArrayList<Uri> list = intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM);
                if (list != null) {
                    for (Uri uri : list) {
                        if (uri != null)
                            files.put(uri.toString());
                    }
                }
            } else {
                Uri uri = intent.getParcelableExtra(Intent.EXTRA_STREAM);
                if (uri != null)
                    files.put(uri.toString());
            }
            share.put("files", files);
            pendingShare = share.toString();
        } catch (Exception e) {
            pendingShare = null;
        }
        // Once only: a rotated screen must not deliver it again.
        intent.setAction(Intent.ACTION_MAIN);
    }

    /** What was shared since the last call, as JSON, or null. */
    public static synchronized String takeShared() {
        String share = pendingShare;
        pendingShare = null;
        return share;
    }

    /** Android 13+ shows nothing from an app that hasn't been allowed to. */
    public static void askForNotifications(Activity activity) {
        if (Build.VERSION.SDK_INT < 33)
            return;
        String permission = "android.permission.POST_NOTIFICATIONS";
        if (activity.checkSelfPermission(permission) != PackageManager.PERMISSION_GRANTED)
            activity.requestPermissions(new String[] {permission}, 4401);
    }
}
