package android.net.ssl;

import javax.net.ssl.SSLSocket;

/** Android 10's helpers for the platform's TLS sockets (Conscrypt's). The TLS here is wolfSSL's JSSE provider, whose sockets
 *  these do not apply to: OkHttp asks isSupportedSocket first and configures its sockets the standard way when they are not. */
public final class SSLSockets {
    private SSLSockets() {}
    public static boolean isSupportedSocket(SSLSocket socket) { return false; }
    public static void setUseSessionTickets(SSLSocket socket, boolean useSessionTickets) {
        throw new IllegalArgumentException("not a platform SSLSocket");
    }
}
