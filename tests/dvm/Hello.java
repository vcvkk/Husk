public class Hello {
    static String text;
    int value = 7;

    // null unless the program has arguments: the compiler cannot see which
    static Hello maybe(String[] args) { return args.length > 0 ? new Hello() : null; }

    public static void main(String[] args) {
        System.out.println("Hello from husk-port's DVM");
        int sum = 0;
        for (int i = 1; i <= 100; i++) sum += i;
        System.out.println("sum 1..100 = " + sum + ", args=" + args.length);
        // implicit null checks: compiled code loads through the null reference, and the fault
        // handler turns the fault into a NullPointerException
        try {
            System.out.println(maybe(args).value);
        } catch (NullPointerException e) {
            System.out.println("field on null: NullPointerException");
        }
        try {
            System.out.println(text.length());
        } catch (NullPointerException e) {
            System.out.println("method on null: NullPointerException");
        }
    }
}
