package org.telegram.tgnet;
public class TLRPC {
    public static class auth_Authorization extends TLObject {
        public static auth_Authorization TLdeserialize(InputSerializedData stream, int constructor, boolean exception) { return null; }
    }
    public static class TL_auth_authorization extends auth_Authorization { public User user; }
    public static class User extends TLObject { public long id; public String username; }
    public static class TL_error extends TLObject { public int code; public String text; }
}
