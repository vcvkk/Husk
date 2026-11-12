package org.telegram.messenger;
import java.util.List;
public class MessagesStorage {
    public static MessagesStorage getInstance(int account) { return null; }
    public void cleanup(boolean isLogin) {}
    public void putUsersAndChats(List users, List chats, boolean withTransaction, boolean useThread) {}
}
