// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.content.ContentResolver;
import android.content.ContentValues;
import android.content.Context;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.provider.MediaStore;
import android.webkit.MimeTypeMap;

import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;

/**
 * The phone's shared Downloads folder, the one its Files app shows.
 *
 * An app can't write there by path: the folder Qt calls "Downloads" is the
 * app's own, which no file manager lists. A file is put into the shared one
 * by handing it to the system's media store.
 */
public final class Downloads {
    private Downloads() {}

    /**
     * Copy the file at `path` into Downloads under `name`. Returns the new
     * file's content URI, or null where that isn't possible (before
     * Android 10) or went wrong.
     */
    public static String publish(Context context, String path, String name) {
        if (Build.VERSION.SDK_INT < 29)
            return null;
        ContentResolver resolver = context.getContentResolver();
        ContentValues values = new ContentValues();
        values.put(MediaStore.Downloads.DISPLAY_NAME, name);
        int dot = name.lastIndexOf('.');
        String type = dot < 0 ? null : MimeTypeMap.getSingleton().getMimeTypeFromExtension(
            name.substring(dot + 1).toLowerCase(Locale.ROOT));
        if (type != null)
            values.put(MediaStore.Downloads.MIME_TYPE, type);
        values.put(MediaStore.Downloads.RELATIVE_PATH, Environment.DIRECTORY_DOWNLOADS);
        // Hidden from other apps until it is whole.
        values.put(MediaStore.Downloads.IS_PENDING, 1);
        Uri uri = null;
        try {
            uri = resolver.insert(MediaStore.Downloads.EXTERNAL_CONTENT_URI, values);
            if (uri == null)
                return null;
            try (InputStream in = new FileInputStream(path);
                 OutputStream out = resolver.openOutputStream(uri)) {
                if (out == null)
                    throw new java.io.IOException("no stream");
                byte[] buffer = new byte[1 << 16];
                for (int n; (n = in.read(buffer)) > 0; )
                    out.write(buffer, 0, n);
            }
            ContentValues done = new ContentValues();
            done.put(MediaStore.Downloads.IS_PENDING, 0);
            resolver.update(uri, done, null, null);
            return uri.toString();
        } catch (Exception e) {
            if (uri != null) {
                try {
                    resolver.delete(uri, null, null);
                } catch (Exception ignored) {
                    // Nothing more to do about a half-written entry.
                }
            }
            return null;
        }
    }
}
