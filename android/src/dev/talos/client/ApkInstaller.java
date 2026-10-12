// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.app.PendingIntent;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInstaller;
import android.os.Build;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;

/**
 * The mesh's install_apk, through Android's own installer.
 *
 * The package is handed to the system, which asks the person holding the
 * device to confirm. Nothing is installed silently: that would take root or
 * Shizuku, which this app does not use.
 */
public class ApkInstaller extends BroadcastReceiver {
    private static final String ACTION = "dev.talos.client.INSTALL_STATUS";

    /** Returns null when the package was handed over, or what went wrong. */
    public static String install(Context context, String path) {
        File apk = new File(path);
        if (!apk.isFile() || !apk.canRead())
            return "No readable APK at " + path;
        if (!context.getPackageManager().canRequestPackageInstalls())
            return "Talos isn't allowed to install apps. Allow it under "
                + "Settings > Apps > Special access > Install unknown apps.";
        PackageInstaller installer = context.getPackageManager().getPackageInstaller();
        PackageInstaller.Session session = null;
        try {
            PackageInstaller.SessionParams params = new PackageInstaller.SessionParams(
                PackageInstaller.SessionParams.MODE_FULL_INSTALL);
            params.setSize(apk.length());
            int id = installer.createSession(params);
            session = installer.openSession(id);
            try (InputStream in = new FileInputStream(apk);
                 OutputStream out = session.openWrite("package", 0, apk.length())) {
                byte[] buffer = new byte[1 << 16];
                for (int n; (n = in.read(buffer)) > 0; )
                    out.write(buffer, 0, n);
                session.fsync(out);
            }
            Intent status = new Intent(context, ApkInstaller.class).setAction(ACTION);
            int flags = PendingIntent.FLAG_UPDATE_CURRENT;
            // The system fills in the result, so the intent has to be mutable.
            if (Build.VERSION.SDK_INT >= 31)
                flags |= PendingIntent.FLAG_MUTABLE;
            session.commit(PendingIntent.getBroadcast(context, id, status, flags).getIntentSender());
            return null;
        } catch (Exception e) {
            if (session != null)
                session.abandon();
            return "The system installer refused the package: " + e.getMessage();
        }
    }

    @Override
    public void onReceive(Context context, Intent intent) {
        if (!ACTION.equals(intent.getAction()))
            return;
        int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS,
                                        PackageInstaller.STATUS_FAILURE);
        if (status != PackageInstaller.STATUS_PENDING_USER_ACTION)
            return;
        // The system's own "install this app?" screen.
        Intent confirm = Build.VERSION.SDK_INT >= 33
            ? intent.getParcelableExtra(Intent.EXTRA_INTENT, Intent.class)
            : (Intent) intent.getParcelableExtra(Intent.EXTRA_INTENT);
        if (confirm == null)
            return;
        confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(confirm);
        } catch (RuntimeException e) {
            // Not allowed from the background; the install waits unconfirmed
            // and the system discards it.
        }
    }
}
