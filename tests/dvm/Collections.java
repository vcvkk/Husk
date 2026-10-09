import java.util.ArrayList;
import java.util.Arrays;
import java.util.Comparator;
import java.util.HashMap;
import java.util.Iterator;
import java.util.List;
import java.util.Map;

public class Collections {
    static class Person implements Comparable<Person> {
        final String name; final int age;
        Person(String n, int a) { name = n; age = a; }
        public int compareTo(Person o) { return Integer.compare(age, o.age); }
        public String toString() { return name + "(" + age + ")"; }
        public boolean equals(Object o) { return o instanceof Person && ((Person) o).name.equals(name) && ((Person) o).age == age; }
        public int hashCode() { return name.hashCode() * 31 + age; }
    }
    public static void main(String[] args) {
        List<Integer> list = new ArrayList<>();
        for (int i = 0; i < 20; i++) list.add(i * 7 % 11);
        int sum = 0;
        for (int v : list) sum += v;
        System.out.println(list + " sum=" + sum + " size=" + list.size() + " idx(3)=" + list.indexOf(3));
        list.remove(0);
        list.add(0, 99);
        System.out.println(list.get(0) + " " + list.get(1) + " " + list.contains(10) + " " + list.contains(42));
        Integer a = 127, b = 127, c = 1000, d = 1000;
        System.out.println("box cache: " + (a == b) + " " + c.equals(d));
        Map<String, Integer> counts = new HashMap<>();
        String text = "the quick brown fox jumps over the lazy dog the end";
        for (String w : text.split(" ")) counts.put(w, counts.getOrDefault(w, 0) + 1);
        System.out.println("the=" + counts.get("the") + " fox=" + counts.get("fox") + " cat=" + counts.get("cat") + " size=" + counts.size());
        Map<Person, String> roles = new HashMap<>();
        for (int i = 0; i < 1000; i++) roles.put(new Person("p" + i, i % 90), "r" + i);
        System.out.println("roles " + roles.size() + " " + roles.get(new Person("p500", 500 % 90)) + " removed " + roles.remove(new Person("p1", 1)) + " " + roles.size());
        Person[] people = { new Person("Ann", 31), new Person("Bob", 25), new Person("Cid", 40), new Person("Dee", 25) };
        Arrays.sort(people);
        System.out.println(Arrays.toString(people));
        Arrays.sort(people, new Comparator<Person>() { public int compare(Person x, Person y) { return x.name.compareTo(y.name); } }.reversed());
        System.out.println(Arrays.toString(people));
        int[] nums = { 5, 3, 9, 1, 7 };
        Arrays.sort(nums);
        System.out.println(Arrays.toString(nums) + " " + Arrays.equals(nums, new int[] { 1, 3, 5, 7, 9 }));
        int[][] grid = new int[3][4];
        for (int i = 0; i < 3; i++) for (int j = 0; j < 4; j++) grid[i][j] = i * j;
        long[][][] cube = new long[2][3][];
        System.out.println(grid[2][3] + " " + grid.length + " " + grid[0].length + " " + cube[1].length + " " + (cube[1][2] == null) + " " + grid.getClass().getName());
        Iterator<Integer> it = list.iterator();
        try { while (it.hasNext()) if (it.next() == 99) list.add(1); } catch (java.util.ConcurrentModificationException e) { System.out.println("CME"); }
        Object[] objs = list.toArray();
        String[] strs = Arrays.asList("x", "y", "z").toArray(new String[0]);
        System.out.println(objs.length + " " + strs.length + " " + strs.getClass().getSimpleName() + " " + strs[2]);
        char[] chars = { 'd', 'v', 'm' };
        char[] copy = chars.clone();
        copy[0] = 'D';
        System.out.println(new String(chars) + " " + new String(copy));
    }
}
