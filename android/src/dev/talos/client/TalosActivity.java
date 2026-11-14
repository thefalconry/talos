// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.Configuration;
import android.graphics.drawable.ColorDrawable;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.HapticFeedbackConstants;
import android.view.View;
import android.view.Window;

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
        // Until the interface says which theme it wears, the bars take the
        // colour of the one it starts in: no black or white flash at launch.
        boolean night = (getResources().getConfiguration().uiMode
                         & Configuration.UI_MODE_NIGHT_MASK) == Configuration.UI_MODE_NIGHT_YES;
        setBars(this, !night, night ? 0xFF262624 : 0xFFFAF9F5);
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
     * The status and navigation bars wear the app's own background, with
     * icons to suit it: dark icons over a light theme, light over a dark.
     *
     * From Android 15 the bars are transparent and the window is drawn
     * behind them, so it is the window's background that shows: that is set
     * here too, for wherever the interface itself does not reach. Earlier
     * versions paint the bars, and are told the colour. Either way the
     * system's own grey veil behind a three-button bar is turned off.
     */
    @SuppressWarnings("deprecation")
    public static void setBars(final Activity activity, final boolean light, final int color) {
        activity.runOnUiThread(() -> {
            Window window = activity.getWindow();
            if (window == null)
                return;
            window.setBackgroundDrawable(new ColorDrawable(color));
            if (Build.VERSION.SDK_INT < 35) {
                window.addFlags(android.view.WindowManager.LayoutParams
                                    .FLAG_DRAWS_SYSTEM_BAR_BACKGROUNDS);
                window.setStatusBarColor(color);
                window.setNavigationBarColor(color);
            }
            if (Build.VERSION.SDK_INT >= 29) {
                window.setStatusBarContrastEnforced(false);
                window.setNavigationBarContrastEnforced(false);
            }
            if (Build.VERSION.SDK_INT < 30) {
                // Android 9 and 10 know only the older flags.
                View decor = window.getDecorView();
                int flags = View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR
                    | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
                int now = decor.getSystemUiVisibility();
                decor.setSystemUiVisibility(light ? now | flags : now & ~flags);
                return;
            }
            android.view.WindowInsetsController bars = window.getInsetsController();
            if (bars == null)
                return;
            int both = android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS
                | android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
            bars.setSystemBarsAppearance(light ? both : 0, both);
        });
    }

    /**
     * The colours Android drew from the wallpaper (Material You), as JSON:
     * tones of the accent ("a200" to "a800") and of the neutral it pairs
     * with them ("n10" to "n900"). Null before Android 12, which has none.
     */
    public static String wallpaperColors(Context context) {
        if (Build.VERSION.SDK_INT < 31)
            return null;
        try {
            JSONObject out = new JSONObject();
            int[][] tones = {
                {200, android.R.color.system_accent1_200, android.R.color.system_neutral1_200},
                {300, android.R.color.system_accent1_300, android.R.color.system_neutral1_300},
                {400, android.R.color.system_accent1_400, android.R.color.system_neutral1_400},
                {500, android.R.color.system_accent1_500, android.R.color.system_neutral1_500},
                {600, android.R.color.system_accent1_600, android.R.color.system_neutral1_600},
                {700, android.R.color.system_accent1_700, android.R.color.system_neutral1_700},
                {800, android.R.color.system_accent1_800, android.R.color.system_neutral1_800},
                {900, android.R.color.system_accent1_900, android.R.color.system_neutral1_900},
            };
            for (int[] tone : tones) {
                out.put("a" + tone[0], hex(context.getColor(tone[1])));
                out.put("n" + tone[0], hex(context.getColor(tone[2])));
            }
            out.put("n10", hex(context.getColor(android.R.color.system_neutral1_10)));
            out.put("n50", hex(context.getColor(android.R.color.system_neutral1_50)));
            out.put("n100", hex(context.getColor(android.R.color.system_neutral1_100)));
            return out.toString();
        } catch (Exception e) {
            return null;
        }
    }

    private static String hex(int color) {
        return String.format("#%06X", color & 0xFFFFFF);
    }

    /** A small tick under the finger: 0 a tap, 1 a long press, 2 done, 3 refused. */
    public static void haptic(final Activity activity, final int kind) {
        activity.runOnUiThread(() -> {
            Window window = activity.getWindow();
            if (window == null)
                return;
            int effect = kind == 1 ? HapticFeedbackConstants.LONG_PRESS
                : kind == 2 && Build.VERSION.SDK_INT >= 30 ? HapticFeedbackConstants.CONFIRM
                : kind == 3 && Build.VERSION.SDK_INT >= 30 ? HapticFeedbackConstants.REJECT
                : HapticFeedbackConstants.VIRTUAL_KEY;
            window.getDecorView().performHapticFeedback(effect);
        });
    }

    /**
     * From Android 17 an app must be allowed to reach the local network
     * before it can open a connection to an address on it, which is where
     * a Talon usually lives.
     */
    private static final String LOCAL_NETWORK = "android.permission.ACCESS_LOCAL_NETWORK";

    public static boolean localNetworkAllowed(Context context) {
        return Build.VERSION.SDK_INT < 37
            || context.checkSelfPermission(LOCAL_NETWORK) == PackageManager.PERMISSION_GRANTED;
    }

    public static void askForLocalNetwork(Activity activity) {
        if (!localNetworkAllowed(activity))
            activity.requestPermissions(new String[] {LOCAL_NETWORK}, 4402);
    }

    /** Whether file commands can reach the phone's shared storage (Android 11+). */
    public static boolean allFilesAllowed() {
        return Build.VERSION.SDK_INT >= 30 && Environment.isExternalStorageManager();
    }

    /** The system's own switch for that, which only the person can turn. */
    public static void askForAllFiles(Context context) {
        if (Build.VERSION.SDK_INT < 30)
            return;
        Intent ask = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                Uri.parse("package:" + context.getPackageName()))
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(ask);
        } catch (RuntimeException e) {
            try {
                context.startActivity(
                    new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION)
                        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            } catch (RuntimeException ignored) {
                // A phone without that screen.
            }
        }
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
