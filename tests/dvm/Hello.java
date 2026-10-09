public class Hello {
    public static void main(String[] args) {
        System.out.println("Hello from husk-port's DVM");
        int sum = 0;
        for (int i = 1; i <= 100; i++) sum += i;
        System.out.println("sum 1..100 = " + sum + ", args=" + args.length);
    }
}
