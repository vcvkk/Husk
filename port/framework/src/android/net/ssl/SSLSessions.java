package android.net.ssl;

import javax.net.ssl.SSLSession;

/** Android 10's helper for the platform's TLS sessions: none of wolfSSL's sessions are the platform's. */
public final class SSLSessions {
    private SSLSessions() {}
    public static boolean isSupportedSslSession(SSLSession session) { return false; }
    public static void exportKeyingMaterial(SSLSession session, String label, byte[] context, int length) {
        throw new IllegalArgumentException("not a platform SSLSession");
    }
}
