package im.status.keycard.android;

import android.nfc.NfcAdapter;
import android.nfc.Tag;
import android.nfc.tech.IsoDep;
import android.util.Log;

import java.io.IOException;

/**
 * NFC Reader callback for Keycard using enableReaderMode()
 * This is the modern Android NFC API approach used by React Native
 */
public class KeycardNfcReader implements NfcAdapter.ReaderCallback {
    private static final String TAG = "KeycardNfcReader";
    private static final int NFC_TIMEOUT_MS = 120000; // 120 seconds (matching React Native)

    private IsoDep isoDep;
    private volatile boolean connected = false;

    // Native callback methods
    private static native void onNativeTagConnected(long nativePtr, Object isoDep);
    private static native void onNativeTagDisconnected(long nativePtr);

    private long nativePtr; // Pointer to C++ KeycardChannelAndroidNfc instance

    public KeycardNfcReader(long nativePtr) {
        this.nativePtr = nativePtr;
        Log.d(TAG, "KeycardNfcReader created with nativePtr: " + nativePtr);
    }

    @Override
    public void onTagDiscovered(Tag tag) {
        Log.d(TAG, "🔍 onTagDiscovered called!");

        try {
            // Get IsoDep technology from tag
            isoDep = IsoDep.get(tag);
            if (isoDep == null) {
                Log.w(TAG, "Tag does not support IsoDep");
                return;
            }

            // Connect to the tag
            isoDep.connect();
            Log.d(TAG, "✅ IsoDep connected!");

            // Set timeout to 120 seconds (matching React Native)
            isoDep.setTimeout(NFC_TIMEOUT_MS);
            Log.d(TAG, "✅ IsoDep timeout set to " + NFC_TIMEOUT_MS + " ms");

            // Check capabilities
            boolean supportsExtended = isoDep.isExtendedLengthApduSupported();
            int maxLength = isoDep.getMaxTransceiveLength();
            Log.d(TAG, "IsoDep extended APDU supported: " + supportsExtended);
            Log.d(TAG, "IsoDep max transceive length: " + maxLength + " bytes");

            // Get tag UID
            byte[] tagId = tag.getId();
            StringBuilder uidHex = new StringBuilder();
            for (byte b : tagId) {
                uidHex.append(String.format("%02x", b));
            }
            Log.d(TAG, "Tag UID: " + uidHex.toString());

            connected = true;

            // Notify C++ side that tag is connected
            onNativeTagConnected(nativePtr, isoDep);

        } catch (IOException e) {
            Log.e(TAG, "Error connecting to IsoDep: " + e.getMessage(), e);
            disconnect();
        } catch (SecurityException e) {
            Log.e(TAG, "Security exception connecting to IsoDep: " + e.getMessage(), e);
            disconnect();
        }
    }

    /**
     * Transmit APDU command to the card
     * Called from C++ via JNI
     */
    public byte[] transceive(byte[] apdu) throws IOException {
        if (isoDep == null || !isoDep.isConnected()) {
            throw new IOException("IsoDep not connected");
        }

        // Log the APDU being sent
        StringBuilder apduHex = new StringBuilder();
        for (byte b : apdu) {
            apduHex.append(String.format("%02x", b));
        }
        Log.d(TAG, "📤 Sending APDU (" + apdu.length + " bytes): " + apduHex.toString());

        // ⚠️ CRITICAL FIX: Workaround for Android NFC IsoDep truncation bug after app restart
        // On first app session, IsoDep returns full responses (66-130 bytes)
        // After app restart, same card/same commands return only 34 bytes (truncated!)
        // This appears to be an Android NFC stack bug where buffers get limited after restart
        
        // Try to force a reconnection if we're in a potentially bad state
        try {
            // Check connection health
            if (!isoDep.isConnected()) {
                Log.w(TAG, "⚠️ IsoDep disconnected, attempting reconnect...");
                isoDep.connect();
                isoDep.setTimeout(NFC_TIMEOUT_MS);
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to reconnect IsoDep: " + e.getMessage());
        }

        // Transceive
        byte[] response = isoDep.transceive(apdu);

        // Log the response received
        StringBuilder responseHex = new StringBuilder();
        for (byte b : response) {
            responseHex.append(String.format("%02x", b));
        }
        Log.d(TAG, "📥 Received response (" + response.length + " bytes): " + responseHex.toString());

        // 🔍 DETECTION & FIX: Check if we got a truncated response
        // (34 bytes = 16 MAC + 16 encrypted + 2 SW)
        // This indicates the Android NFC IsoDep truncation bug is active
        if (response.length == 34 && apdu.length >= 38) {
            Log.w(TAG, "⚠️ TRUNCATION DETECTED: Got 34 bytes for " + apdu.length + "-byte command");
            Log.w(TAG, "⚠️ Attempting workaround: Disconnect/reconnect and retry...");
            
            try {
                // Force a complete disconnect/reconnect cycle
                isoDep.close();
                Thread.sleep(100); // Brief delay to let Android NFC stack reset
                isoDep.connect();
                isoDep.setTimeout(NFC_TIMEOUT_MS);
                
                Log.d(TAG, "✅ Reconnected IsoDep, retrying command...");
                
                // Retry the command
                byte[] retryResponse = isoDep.transceive(apdu);
                
                // Log retry result
                StringBuilder retryHex = new StringBuilder();
                for (byte b : retryResponse) {
                    retryHex.append(String.format("%02x", b));
                }
                Log.d(TAG, "📥 RETRY response (" + retryResponse.length + " bytes): " + retryHex.toString());
                
                if (retryResponse.length > 34) {
                    Log.d(TAG, "✅ WORKAROUND SUCCESSFUL: Got " + retryResponse.length + " bytes after reconnect!");
                    return retryResponse;
                } else {
                    Log.w(TAG, "⚠️ Reconnect didn't help, still got " + retryResponse.length + " bytes");
                }
            } catch (Exception e) {
                Log.e(TAG, "❌ Reconnect/retry failed: " + e.getMessage());
                // Fall through to return original truncated response
            }
        }

        return response;
    }

    /**
     * Check if connected to a card
     */
    public boolean isConnected() {
        try {
            return connected && isoDep != null && isoDep.isConnected();
        } catch (SecurityException e) {
            return false;
        }
    }

    /**
     * Disconnect from the card
     */
    public void disconnect() {
        Log.d(TAG, "Disconnecting from IsoDep");
        connected = false;

        if (isoDep != null) {
            try {
                isoDep.close();
            } catch (IOException e) {
                Log.e(TAG, "Error closing IsoDep: " + e.getMessage());
            } catch (SecurityException e) {
                Log.e(TAG, "Security exception closing IsoDep: " + e.getMessage());
            }
            isoDep = null;
        }

        // Notify C++ side
        onNativeTagDisconnected(nativePtr);
    }
}

// CMake dependency tracking test - Wed Nov  5 22:27:13 EET 2025
