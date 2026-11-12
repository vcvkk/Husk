package org.telegram.tgnet;
public interface OutputSerializedData {
    void writeInt32(int x);
    void writeString(String s);
}
