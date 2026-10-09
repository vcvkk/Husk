package java.lang;

public class AssertionError extends Error {
    public AssertionError() { super(); }
    public AssertionError(String message) { super(message); }
    public AssertionError(String message, Throwable cause) { super(message, cause); }
    public AssertionError(Throwable cause) { super(cause); }
    public AssertionError(Object detail) { super(String.valueOf(detail)); if (detail instanceof Throwable) initCause((Throwable) detail); }
}
