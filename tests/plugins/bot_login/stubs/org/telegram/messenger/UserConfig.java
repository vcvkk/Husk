package org.telegram.messenger;
import org.telegram.tgnet.TLRPC;
public class UserConfig {
    public static int selectedAccount;
    public boolean syncContacts;
    public static UserConfig getInstance(int account) { return null; }
    public boolean isClientActivated() { return false; }
    public void clearConfig() {}
    public void setCurrentUser(TLRPC.User user) {}
    public void saveConfig(boolean withFile) {}
}
