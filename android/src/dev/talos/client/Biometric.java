// SPDX-License-Identifier: GPL-2.0-only
package dev.talos.client;

import android.app.Activity;
import android.content.Context;
import android.content.SharedPreferences;
import android.hardware.biometrics.BiometricManager;
import android.hardware.biometrics.BiometricPrompt;
import android.os.Build;
import android.os.CancellationSignal;
import android.security.keystore.KeyGenParameterSpec;
import android.security.keystore.KeyProperties;
import android.util.Base64;

import java.security.KeyStore;

import javax.crypto.Cipher;
import javax.crypto.KeyGenerator;
import javax.crypto.SecretKey;
import javax.crypto.spec.GCMParameterSpec;

/**
 * Keeps the app lock's data key under a keystore key that Android only
 * lets be used after a successful biometric prompt. The app never holds a
 * usable copy while locked: decrypting goes through the prompt every time.
 */
public final class Biometric {
    private static final String KEY_ALIAS = "talos_applock_bio";
    private static final String PREFS = "talos_biometric";
    private static final String STORE = "AndroidKeyStore";

    private static CancellationSignal running;

    private Biometric() {}

    // Implemented in C++ (src/app/Biometric.cpp).
    private static native void sealed(long handle, boolean ok, String error);
    private static native void opened(long handle, byte[] secret, String error);

    private interface Unlocked {
        void run(Cipher cipher) throws Exception;
    }

    public static boolean available(Context context) {
        if (Build.VERSION.SDK_INT < 30) {
            // Before Android 11 the system cannot be asked for a *strong*
            // biometric specifically, so the feature is not offered.
            return false;
        }
        BiometricManager manager = context.getSystemService(BiometricManager.class);
        return manager != null
                && manager.canAuthenticate(BiometricManager.Authenticators.BIOMETRIC_STRONG)
                        == BiometricManager.BIOMETRIC_SUCCESS;
    }

    public static boolean enrolled(Context context) {
        return prefs(context).contains("ct");
    }

    public static void clear(Context context) {
        prefs(context).edit().clear().apply();
        try {
            KeyStore store = KeyStore.getInstance(STORE);
            store.load(null);
            store.deleteEntry(KEY_ALIAS);
        } catch (Exception ignored) {
            // Nothing to delete.
        }
    }

    public static void cancel() {
        CancellationSignal signal = running;
        if (signal != null) {
            signal.cancel();
        }
    }

    public static void seal(final Activity activity, final byte[] secret, final long handle) {
        try {
            KeyGenerator generator =
                    KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, STORE);
            KeyGenParameterSpec.Builder spec = new KeyGenParameterSpec.Builder(
                    KEY_ALIAS, KeyProperties.PURPOSE_ENCRYPT | KeyProperties.PURPOSE_DECRYPT)
                    .setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                    .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                    .setKeySize(256)
                    .setUserAuthenticationRequired(true)
                    // A newly enrolled finger must not inherit the unlock.
                    .setInvalidatedByBiometricEnrollment(true);
            if (Build.VERSION.SDK_INT >= 30) {
                spec.setUserAuthenticationParameters(0, KeyProperties.AUTH_BIOMETRIC_STRONG);
            }
            generator.init(spec.build());
            SecretKey key = generator.generateKey();

            final Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            cipher.init(Cipher.ENCRYPT_MODE, key);
            prompt(activity, cipher, "Turn on biometric unlock",
                    new Unlocked() {
                        @Override
                        public void run(Cipher unlocked) throws Exception {
                            byte[] sealedKey = unlocked.doFinal(secret);
                            prefs(activity).edit()
                                    .putString("iv", Base64.encodeToString(
                                            unlocked.getIV(), Base64.NO_WRAP))
                                    .putString("ct", Base64.encodeToString(
                                            sealedKey, Base64.NO_WRAP))
                                    .apply();
                            sealed(handle, true, null);
                        }
                    },
                    new java.util.function.Consumer<String>() {
                        @Override
                        public void accept(String error) {
                            sealed(handle, false, error);
                        }
                    });
        } catch (Exception e) {
            sealed(handle, false, "Couldn't set up biometric unlock: " + e.getMessage());
        }
    }

    public static void open(final Activity activity, final long handle) {
        try {
            SharedPreferences prefs = prefs(activity);
            final byte[] iv = Base64.decode(prefs.getString("iv", ""), Base64.NO_WRAP);
            final byte[] ct = Base64.decode(prefs.getString("ct", ""), Base64.NO_WRAP);
            KeyStore store = KeyStore.getInstance(STORE);
            store.load(null);
            SecretKey key = (SecretKey) store.getKey(KEY_ALIAS, null);
            if (key == null || ct.length == 0) {
                opened(handle, null, "Biometric unlock isn't set up.");
                return;
            }
            final Cipher cipher = Cipher.getInstance("AES/GCM/NoPadding");
            // Throws when the enrolled biometrics changed since sealing:
            // the key is gone for good and the passcode is the way in.
            cipher.init(Cipher.DECRYPT_MODE, key, new GCMParameterSpec(128, iv));
            prompt(activity, cipher, "Unlock Talos",
                    new Unlocked() {
                        @Override
                        public void run(Cipher unlocked) throws Exception {
                            opened(handle, unlocked.doFinal(ct), null);
                        }
                    },
                    new java.util.function.Consumer<String>() {
                        @Override
                        public void accept(String error) {
                            opened(handle, null, error);
                        }
                    });
        } catch (android.security.keystore.KeyPermanentlyInvalidatedException e) {
            clear(activity);
            opened(handle, null,
                    "The device's biometrics changed. Unlock with the passcode and turn "
                            + "biometric unlock on again.");
        } catch (Exception e) {
            opened(handle, null, "Biometric unlock failed: " + e.getMessage());
        }
    }

    private static void prompt(final Activity activity, final Cipher cipher, final String title,
            final Unlocked onSuccess, final java.util.function.Consumer<String> onFailure) {
        activity.runOnUiThread(new Runnable() {
            @Override
            public void run() {
                final CancellationSignal signal = new CancellationSignal();
                running = signal;
                BiometricPrompt prompt = new BiometricPrompt.Builder(activity)
                        .setTitle(title)
                        .setNegativeButton("Use passcode", activity.getMainExecutor(),
                                (dialog, which) -> {
                                    running = null;
                                    onFailure.accept("Cancelled");
                                })
                        .build();
                prompt.authenticate(new BiometricPrompt.CryptoObject(cipher), signal,
                        activity.getMainExecutor(),
                        new BiometricPrompt.AuthenticationCallback() {
                            @Override
                            public void onAuthenticationSucceeded(
                                    BiometricPrompt.AuthenticationResult result) {
                                running = null;
                                try {
                                    onSuccess.run(result.getCryptoObject().getCipher());
                                } catch (Exception e) {
                                    onFailure.accept("Biometric unlock failed: " + e.getMessage());
                                }
                            }

                            @Override
                            public void onAuthenticationError(int code, CharSequence message) {
                                running = null;
                                onFailure.accept(String.valueOf(message));
                            }
                            // onAuthenticationFailed: a non-matching attempt;
                            // the system prompt stays up and says so itself.
                        });
            }
        });
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }
}
