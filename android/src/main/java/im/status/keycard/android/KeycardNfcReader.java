package im.status.keycard.android;

import android.app.Activity;
import android.app.Application;
import android.nfc.NfcAdapter;
import android.nfc.Tag;
import android.nfc.TagLostException;
import android.nfc.tech.IsoDep;
import android.os.Bundle;
import android.app.Dialog;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.ColorDrawable;
import android.graphics.drawable.GradientDrawable;
import android.util.Log;
import android.view.Gravity;
import android.view.ViewGroup;
import android.view.Window;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.TextView;

import java.io.IOException;
import java.util.HashSet;
import java.util.Set;

/**
 * GLOBAL SINGLETON NFC Reader callback for Keycard
 * 
 * This singleton is registered ONCE with Android's NFC stack and routes
 * tag events to multiple C++ backends (SessionManager, FlowManager, etc.)
 */
public class KeycardNfcReader implements NfcAdapter.ReaderCallback {
    private static final String TAG = "KeycardNfcReader";
    private static final int NFC_TIMEOUT_MS = 120000;
    private static final int DETECTION_CHECK_INTERVAL_MS = 500;

    private static KeycardNfcReader instance;
    private static final Object lock = new Object();

    private IsoDep isoDep;
    private volatile boolean detectionLoopActive = false;
    private Thread detectionThread;
    
    private Activity currentActivity;
    private NfcAdapter nfcAdapter;
    private boolean lifecycleCallbacksRegistered = false;
    
    // UI Elements
    private Dialog nfcDrawerDialog;
    private TextView drawerTitle;
    private TextView drawerMessage;
    private TextView drawerIcon;

    private final Set<Long> registeredBackends = new HashSet<>();

    private static native void onNativeTagConnected(long nativePtr, Object isoDep);
    private static native void onNativeTagDisconnected(long nativePtr);
    
    private final Application.ActivityLifecycleCallbacks lifecycleCallbacks = new Application.ActivityLifecycleCallbacks() {
        @Override
        public void onActivityCreated(Activity activity, Bundle savedInstanceState) {}
        
        @Override
        public void onActivityStarted(Activity activity) {}
        
        @Override
        public void onActivityResumed(Activity activity) {}

        @Override
        public void onActivityPaused(Activity activity) {}

        @Override
        public void onActivityStopped(Activity activity) {}

        @Override
        public void onActivitySaveInstanceState(Activity activity, Bundle outState) {}

        @Override
        public void onActivityDestroyed(Activity activity) {
            if (activity == currentActivity) {
                currentActivity = null;
            }
        }
    };

    public static KeycardNfcReader getInstance() {
        if (instance == null) {
            synchronized (lock) {
                if (instance == null) {
                    instance = new KeycardNfcReader();
                }
            }
        }
        return instance;
    }

    private KeycardNfcReader() {}

    public void registerBackend(long nativePtr) {
        synchronized (registeredBackends) {
            registeredBackends.add(nativePtr);
        }
    }

    public void unregisterBackend(long nativePtr) {
        synchronized (registeredBackends) {
            registeredBackends.remove(nativePtr);
        }
    }


    public void enableReaderMode(final Activity activity) {
        if (activity != null) {
            currentActivity = activity;
            
            if (!lifecycleCallbacksRegistered) {
                try {
                    activity.getApplication().registerActivityLifecycleCallbacks(lifecycleCallbacks);
                    lifecycleCallbacksRegistered = true;
                } catch (Exception e) {
                    Log.e(TAG, "Failed to register lifecycle callbacks", e);
                }
            }
        }
        
        if (currentActivity == null) {
            return;
        }
        
        final Runnable enableTask = () -> {
            try {
                nfcAdapter = NfcAdapter.getDefaultAdapter(currentActivity);
                if (nfcAdapter == null) {
                    Log.e(TAG, "NFC adapter not available");
                    return;
                }

                int flags = NfcAdapter.FLAG_READER_NFC_A | NfcAdapter.FLAG_READER_SKIP_NDEF_CHECK;
                nfcAdapter.enableReaderMode(currentActivity, KeycardNfcReader.this, flags, null);
                showNfcDrawer(currentActivity);
                startDetectionLoop();
            } catch (Exception e) {
                Log.e(TAG, "Failed to enable reader mode", e);
            }
        };

        if (currentActivity.getMainLooper().getThread() == Thread.currentThread()) {
            enableTask.run();
        } else {
            currentActivity.runOnUiThread(enableTask);
        }
    }

    private void disableReaderMode() {
        stopDetectionLoop();
        hideNfcDrawer();
        
        if (nfcAdapter != null && currentActivity != null) {
            try {
                nfcAdapter.disableReaderMode(currentActivity);
            } catch (Exception e) {
                Log.e(TAG, "Failed to disable reader mode", e);
            }
        }
    }

    public void forceScan() {
        disableReaderMode();
        if (currentActivity != null) {
            enableReaderMode(currentActivity);
        }
    }

    @Override
    public void onTagDiscovered(Tag tag) {
        try {
            IsoDep newIsoDep = IsoDep.get(tag);
            if (newIsoDep == null) {
                return;
            }

            synchronized (lock) {
                if (isoDep != null) {
                    try {
                        if (isoDep.isConnected() && isSameTag(isoDep.getTag(), tag)) {
                            return;
                        }
                    } catch (SecurityException e) {
                        // Tag is stale/out of date, proceed with new connection
                    }
                    
                    try {
                        isoDep.close();
                    } catch (IOException | SecurityException e) {
                        // Ignore
                    }
                    isoDep = null;
                }
            }

            newIsoDep.setTimeout(NFC_TIMEOUT_MS);
            newIsoDep.connect();
            
            synchronized (lock) {
                isoDep = newIsoDep;
            }
        } catch (IOException | SecurityException e) {
            Log.e(TAG, "Failed to connect IsoDep", e);
        }
    }

    private boolean isSameTag(Tag tag1, Tag tag2) {
        if (tag1 == null || tag2 == null) {
            return false;
        }
        
        byte[] uid1 = tag1.getId();
        byte[] uid2 = tag2.getId();
        
        if (uid1 == null || uid2 == null || uid1.length != uid2.length) {
            return false;
        }
        
        for (int i = 0; i < uid1.length; i++) {
            if (uid1[i] != uid2[i]) {
                return false;
            }
        }
        
        return true;
    }

    public boolean isConnected() {
        try {
            synchronized (lock) {
                return isoDep != null && isoDep.isConnected();
            }
        } catch (SecurityException e) {
            return false;
        }
    }

    public synchronized byte[] transceive(byte[] apdu) throws IOException {
        setDrawerStateReading();

        IsoDep currentIsoDep;
        synchronized (lock) {
            currentIsoDep = isoDep;
        }
        
        if (currentIsoDep == null || !currentIsoDep.isConnected()) {
            throw new IOException("IsoDep not connected");
        }
        
        int retryCount = 3;
        
        for (int attempt = 0; attempt < retryCount; attempt++) {
            try {
                return currentIsoDep.transceive(apdu);
            } catch (TagLostException e) {
                Log.e(TAG, "Transceive failed (TagLost), attempt " + (attempt + 1) + "/" + retryCount, e);
                
                // Wait before retrying, unless this was the last attempt
                if (attempt < retryCount - 1) {
                    try {
                        Thread.sleep(100);
                    } catch (InterruptedException ie) {
                        Thread.currentThread().interrupt();
                        return null;
                    }
                }
            } catch (IOException e) {
                Log.e(TAG, "Transceive failed (IO)", e);
                return null;
            }
        }
        
        // All retries exhausted
        return null;
    }

    private void startDetectionLoop() {
        if (detectionLoopActive) {
            return;
        }

        detectionLoopActive = true;
        detectionThread = new Thread(() -> {
            boolean previouslyConnected = false;
            
            while (detectionLoopActive) {
                try {
                    Thread.sleep(DETECTION_CHECK_INTERVAL_MS);
                    
                    boolean currentlyConnected = isConnected();
                    
                    if (currentlyConnected && !previouslyConnected) {
                        onTagConnected();
                    } else if (!currentlyConnected && previouslyConnected) {
                        onTagDisconnected();
                    }
                    
                    previouslyConnected = currentlyConnected;
                } catch (InterruptedException e) {
                    break;
                } catch (Exception e) {
                    Log.e(TAG, "Detection loop error", e);
                }
            }
        });
        
        detectionThread.setDaemon(true);
        detectionThread.start();
    }

    private void stopDetectionLoop() {
        detectionLoopActive = false;
        
        if (detectionThread != null && detectionThread.isAlive()) {
            detectionThread.interrupt();
            try {
                detectionThread.join(1000);
            } catch (InterruptedException e) {
                // Ignore
            }
        }
        detectionThread = null;
    }

    private void onTagConnected() {
        Set<Long> backends;
        synchronized (registeredBackends) {
            backends = new HashSet<>(registeredBackends);
        }
        
        IsoDep currentIsoDep;
        synchronized (lock) {
            currentIsoDep = isoDep;
        }
        
        for (Long nativePtr : backends) {
            try {
                onNativeTagConnected(nativePtr, currentIsoDep);
            } catch (Exception e) {
                Log.e(TAG, "Backend connection notification failed", e);
            }
        }
    }

    private void onTagDisconnected() {
        synchronized (lock) {
            if (isoDep != null) {
                try {
                    isoDep.close();
                } catch (IOException | SecurityException e) {
                    // Ignore
                }
                isoDep = null;
            }
        }
        
        Set<Long> backends;
        synchronized (registeredBackends) {
            backends = new HashSet<>(registeredBackends);
        }
        
        for (Long nativePtr : backends) {
            try {
                onNativeTagDisconnected(nativePtr);
            } catch (Exception e) {
                Log.e(TAG, "Backend disconnection notification failed", e);
            }
        }
    }

    public void disconnect() {
        disableReaderMode();
    }

    private void showNfcDrawer(Activity activity) {
        if (nfcDrawerDialog != null && nfcDrawerDialog.isShowing()) {
            // Reset text in case it was in "Reading" state
            if (drawerTitle != null) drawerTitle.setText("Ready to Scan");
            if (drawerMessage != null) drawerMessage.setText("Hold your Keycard near the top back of your phone.");
            return;
        }

        try {
            nfcDrawerDialog = new Dialog(activity);
            nfcDrawerDialog.requestWindowFeature(Window.FEATURE_NO_TITLE);
            
            // Helper for dp to px
            float density = activity.getResources().getDisplayMetrics().density;
            int p16 = (int)(16 * density);
            int p20 = (int)(20 * density);
            int p24 = (int)(24 * density);
            int p30 = (int)(30 * density);
            int p40 = (int)(40 * density);
            
            // Container with rounded top corners
            LinearLayout layout = new LinearLayout(activity);
            layout.setOrientation(LinearLayout.VERTICAL);
            // Add extra bottom padding for safe area approximation
            layout.setPadding(p24, p30, p24, p40); 
            
            GradientDrawable background = new GradientDrawable();
            background.setColor(Color.WHITE);
            // Top-left, Top-right rounded corners (radius 16dp)
            background.setCornerRadii(new float[]{p16, p16, p16, p16, 0, 0, 0, 0});
            layout.setBackground(background);
            
            // Icon (Signal/Wave representation)
            drawerIcon = new TextView(activity);
            drawerIcon.setText("((•))"); 
            drawerIcon.setTextSize(32);
            drawerIcon.setTextColor(Color.parseColor("#007AFF")); // iOS Blue-ish
            drawerIcon.setGravity(Gravity.CENTER);
            drawerIcon.setPadding(0, 0, 0, p20);
            layout.addView(drawerIcon);
            
            // Title
            drawerTitle = new TextView(activity);
            drawerTitle.setText("Ready to Scan");
            drawerTitle.setTextSize(20);
            drawerTitle.setTypeface(Typeface.DEFAULT_BOLD);
            drawerTitle.setTextColor(Color.BLACK);
            drawerTitle.setGravity(Gravity.CENTER);
            drawerTitle.setPadding(0, 0, 0, p16);
            layout.addView(drawerTitle);
            
            // Message
            drawerMessage = new TextView(activity);
            drawerMessage.setText("Hold your Keycard near the top back of your phone.");
            drawerMessage.setTextSize(16);
            drawerMessage.setTextColor(Color.DKGRAY);
            drawerMessage.setGravity(Gravity.CENTER);
            drawerMessage.setPadding(0, 0, 0, p30);
            layout.addView(drawerMessage);
            
            // Cancel Button
            Button cancelButton = new Button(activity);
            cancelButton.setText("Cancel");
            cancelButton.setTextColor(Color.parseColor("#007AFF"));
            cancelButton.setBackgroundColor(Color.TRANSPARENT);
            cancelButton.setAllCaps(false);
            cancelButton.setTextSize(17);
            cancelButton.setOnClickListener(v -> {
                nfcDrawerDialog.dismiss();
            });
            layout.addView(cancelButton);
            
            nfcDrawerDialog.setContentView(layout);
            nfcDrawerDialog.setCancelable(true);
            nfcDrawerDialog.setOnCancelListener(dialog -> {
                // Just dismiss, don't disable reader mode
            });
            
            // Window attributes
            Window window = nfcDrawerDialog.getWindow();
            if (window != null) {
                window.setBackgroundDrawable(new ColorDrawable(Color.TRANSPARENT));
                window.setLayout(ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.WRAP_CONTENT);
                window.setGravity(Gravity.BOTTOM);
                
                // Dim background
                WindowManager.LayoutParams params = window.getAttributes();
                params.dimAmount = 0.5f;
                params.flags |= WindowManager.LayoutParams.FLAG_DIM_BEHIND;
                window.setAttributes(params);
            }
            
            nfcDrawerDialog.show();
        } catch (Exception e) {
            Log.e(TAG, "Failed to show NFC drawer", e);
        }
    }

    private void setDrawerStateReading() {
        if (currentActivity != null) {
            currentActivity.runOnUiThread(() -> {
                if (nfcDrawerDialog == null) showNfcDrawer(currentActivity);

                if (nfcDrawerDialog != null) {
                    if (!nfcDrawerDialog.isShowing()) nfcDrawerDialog.show();
                    if (drawerTitle != null) drawerTitle.setText("Reading...");
                    if (drawerMessage != null) drawerMessage.setText("Hold still...");
                }
            });
        }
    }

    private void setDrawerKeycardLost() {
        if (currentActivity != null) {
            currentActivity.runOnUiThread(() -> {
                if (nfcDrawerDialog == null) showNfcDrawer(currentActivity);
                if (nfcDrawerDialog != null) {
                    if (!nfcDrawerDialog.isShowing()) nfcDrawerDialog.show();
                    if (drawerTitle != null) drawerTitle.setText("Keycard Lost");
                    if (drawerMessage != null) drawerMessage.setText("Please tap your Keycard again.");
                }
            });
        }
    }

    private void hideNfcDrawer() {
        if (currentActivity != null) {
            currentActivity.runOnUiThread(() -> {
                if (nfcDrawerDialog != null) {
                    try {
                        nfcDrawerDialog.dismiss();
                    } catch (Exception e) {
                        // Ignore
                    }
                    nfcDrawerDialog = null;
                }
            });
        }
    }
}

// CMake dependency tracking test - Wed Nov  5 22:27:13 EET 2025

