package org.telegram.messenger;
import org.telegram.tgnet.TLRPC;
public class MessagesController {
    public static MessagesController getInstance(int account) { return null; }
    public void cleanup() {}
    public boolean putUser(TLRPC.User user, boolean fromCache) { return false; }
    public void loadAppConfig() {}
}
