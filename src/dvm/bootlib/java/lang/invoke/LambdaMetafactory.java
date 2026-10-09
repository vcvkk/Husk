package java.lang.invoke;

public final class LambdaMetafactory {
    public static CallSite metafactory(MethodHandles.Lookup caller, String name, MethodType type, MethodType samType,
                                       MethodHandle impl, MethodType instantiatedType) {
        throw new UnsupportedOperationException("lambdas are desugared by d8");
    }
    public static CallSite altMetafactory(MethodHandles.Lookup caller, String name, MethodType type, Object... args) {
        throw new UnsupportedOperationException("lambdas are desugared by d8");
    }
}
