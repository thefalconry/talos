// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;

/** Starts the background connection after a reboot or an update, if it was on. */
public class BootReceiver extends BroadcastReceiver {
    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        if (!Intent.ACTION_BOOT_COMPLETED.equals(action)
            && !Intent.ACTION_MY_PACKAGE_REPLACED.equals(action))
            return;
        if (MeshHost.wanted(context))
            MeshHost.start(context);
    }
}
