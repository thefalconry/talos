// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.app.Activity;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.view.View;

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
    /** On a notification's intent: the chat its reply arrived in. */
    static final String EXTRA_CHAT = "dev.talos.client.chat";

    private static String pendingShare;
    private static String pendingChat;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Qt's QtInputDelegate.showKeyboard() installs a WindowInsetsAnimation.Callback
        // on decorView every time focus enters a text field, clearing it only inside
        // onEnd(). When the keyboard is already open and focus moves to another field,
        // Android runs no IME open animation, onEnd() never fires, and the leaked
        // callback stops DecorView from applying the IME bottom inset (collapsing
        // adjustResize back to the full screen height). Keeping that callback cleared
        // lets DecorView and QML observe consistent IME insets across focus switches.
        if (Build.VERSION.SDK_INT >= 30 && getWindow() != null) {
            final View decor = getWindow().getDecorView();
            if (decor != null) {
                decor.setOnApplyWindowInsetsListener((v, insets) -> {
                    v.setWindowInsetsAnimationCallback(null);
                    return v.onApplyWindowInsets(insets);
                });
                decor.getViewTreeObserver().addOnPreDrawListener(() -> {
                    decor.setWindowInsetsAnimationCallback(null);
                    return true;
                });
            }
        }
        capture(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        capture(intent);
    }

    /** Remember a share until the app, which may still be starting, asks for it. */
    @SuppressWarnings("deprecation")
    private static synchronized void capture(Intent intent) {
        if (intent == null)
            return;
        String chat = intent.getStringExtra(EXTRA_CHAT);
        if (chat != null) {
            pendingChat = chat;
            // Once only, as with a share below.
            intent.removeExtra(EXTRA_CHAT);
        }
        String action = intent.getAction();
        if (!Intent.ACTION_SEND.equals(action) && !Intent.ACTION_SEND_MULTIPLE.equals(action))
            return;
        try {
            JSONObject share = new JSONObject();
            CharSequence text = intent.getCharSequenceExtra(Intent.EXTRA_TEXT);
            share.put("text", text == null ? "" : text.toString());
            JSONArray files = new JSONArray();
            if (Intent.ACTION_SEND_MULTIPLE.equals(action)) {
                ArrayList<Uri> list = Build.VERSION.SDK_INT >= 33
                    ? intent.getParcelableArrayListExtra(Intent.EXTRA_STREAM, Uri.class)
                    : intent.<Uri>getParcelableArrayListExtra(Intent.EXTRA_STREAM);
                if (list != null) {
                    for (Uri uri : list) {
                        if (uri != null)
                            files.put(uri.toString());
                    }
                }
            } else {
                Uri uri = Build.VERSION.SDK_INT >= 33
                    ? intent.getParcelableExtra(Intent.EXTRA_STREAM, Uri.class)
                    : intent.<Uri>getParcelableExtra(Intent.EXTRA_STREAM);
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

    /** The chat whose notification was tapped since the last call, or null. */
    public static synchronized String takeOpenChat() {
        String chat = pendingChat;
        pendingChat = null;
        return chat;
    }

    /** What was shared since the last call, as JSON, or null. */
    public static synchronized String takeShared() {
        String share = pendingShare;
        pendingShare = null;
        return share;
    }

    /**
     * The app is drawn under the status and gesture bars, so their icons
     * have to suit its theme: dark icons over the light theme, light ones
     * over the dark.
     */
    @SuppressWarnings("deprecation")
    public static void setLightBars(final Activity activity, final boolean light) {
        activity.runOnUiThread(() -> {
            if (Build.VERSION.SDK_INT < 30) {
                // Android 9 and 10 know only the older flags.
                View decor = activity.getWindow().getDecorView();
                int flags = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
                    | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
                int now = decor.getSystemUiVisibility();
                decor.setSystemUiVisibility(light ? now | flags : now & ~flags);
                return;
            }
            android.view.WindowInsetsController bars = activity.getWindow().getInsetsController();
            if (bars == null)
                return;
            int both = android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                | android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
            bars.setSystemBarsAppearance(light ? both : 0, both);
        });
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
