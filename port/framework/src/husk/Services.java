package husk;

import android.app.Service;
import android.content.*;
import android.os.Handler;
import android.os.IBinder;
import android.os.Looper;
import java.util.HashMap;

/** The app's own services (started and bound in this process, as on Android) and the system services Husk answers for. */
public final class Services {
    private static final HashMap<String, Service> sRunning = new HashMap<>();
    private static final HashMap<ServiceConnection, String> sBound = new HashMap<>();
    private Services() {}
    private static Manifest.Component find(Intent i) {
        if (i.getComponent() != null) { for (Manifest.Component c : Manifest.services) if (c.name.equals(i.getComponent().getClassName())) return c; }
        if (i.getAction() != null) for (Manifest.Component c : Manifest.services) for (Manifest.Filter f : c.filters) if (f.actions.contains(i.getAction())) return c;
        return null;
    }
    private static Service create(Manifest.Component c) throws Exception {
        Service s = sRunning.get(c.name);
        if (s == null) {
            s = (Service) Class.forName(c.name, true, AppRunner.classLoader()).newInstance();
            s.huskAttach(AppRunner.application() != null ? AppRunner.application() : ContextImpl.app());
            sRunning.put(c.name, s);
            s.onCreate();
        }
        return s;
    }
    public static ComponentName start(Context from, Intent i) {
        Manifest.Component c = find(i);
        if (c == null) return null;
        new Handler(Looper.getMainLooper()).post(() -> {
            try { Service s = create(c); s.onStartCommand(i, 0, 1); } catch (Throwable t) { android.util.Log.e("Husk", "service " + c.name + " failed", t); }
        });
        return new ComponentName(Native.packageName(), c.name);
    }
    public static boolean stop(Intent i) {
        Manifest.Component c = find(i);
        if (c == null) return false;
        Service s = sRunning.remove(c.name);
        if (s != null) s.onDestroy();
        return s != null;
    }
    public static void stopSelf(Service s) { String k = null; for (java.util.Map.Entry<String, Service> e : sRunning.entrySet()) if (e.getValue() == s) k = e.getKey(); if (k != null) { sRunning.remove(k); s.onDestroy(); } }
    public static boolean bind(Context from, Intent i, ServiceConnection conn) {
        final IBinder play = PlayStore.bind(i);
        if (play != null) {
            final ComponentName cn = new ComponentName(GooglePackages.VENDING, i.getAction());
            new Handler(Looper.getMainLooper()).post(() -> conn.onServiceConnected(cn, play));
            return true;
        }
        Manifest.Component c = find(i);
        if (c == null) return false;
        new Handler(Looper.getMainLooper()).post(() -> {
            try {
                Service s = create(c);
                IBinder b = s.onBind(i);
                sBound.put(conn, c.name);
                ComponentName cn = new ComponentName(Native.packageName(), c.name);
                if (b != null) conn.onServiceConnected(cn, b); else conn.onNullBinding(cn);
            } catch (Throwable t) { android.util.Log.e("Husk", "binding " + c.name + " failed", t); }
        });
        return true;
    }
    public static void unbind(ServiceConnection conn) { sBound.remove(conn); }
    /** JobScheduler: start the JobService named on the job and call onStartJob on the main thread. */
    /* A scheduled job runs on the main thread no sooner than its latency (a periodic one each interval, from its first), and one
     * scheduled again with its id replaces it. A job rescheduled within a second of running (WorkManager's work whose
     * constraints it found unmet runs again at once) waits twice as long each time, up to minutes: it went round 500 times a
     * second. */
    private static final java.util.HashMap<Integer, Runnable> sPendingJobs = new java.util.HashMap<>();
    private static final java.util.HashMap<String, long[]> sJobRuns = new java.util.HashMap<>();   /* key -> last run (uptime), streak */
    private static Handler sJobs;
    public static void scheduleJob(final android.app.job.JobInfo j) {
        if (j == null || j.getService() == null) return;
        if (sJobs == null) sJobs = new Handler(Looper.getMainLooper());
        final String key = jobKey(j);
        long delay = j.isPeriodic() ? j.getIntervalMillis() : j.getMinLatencyMillis();
        long now = android.os.SystemClock.uptimeMillis();
        synchronized (sJobRuns) {
            long[] r = sJobRuns.get(key);
            if (r != null && now - r[0] < 1000) { r[1] = Math.min(r[1] + 1, 9); delay = Math.max(delay, 1000L << r[1]); }
            else if (r != null) r[1] = 0;
        }
        Runnable run = new Runnable() {
            @Override public void run() {
                synchronized (sPendingJobs) { if (sPendingJobs.get(j.getId()) != this) return; sPendingJobs.remove(j.getId()); }
                synchronized (sJobRuns) {
                    long[] r = sJobRuns.get(key);
                    if (r == null) sJobRuns.put(key, r = new long[2]);
                    r[0] = android.os.SystemClock.uptimeMillis();
                }
                startJob(j);
                if (j.isPeriodic()) scheduleJob(j);
            }
        };
        synchronized (sPendingJobs) {
            Runnable old = sPendingJobs.put(j.getId(), run);
            if (old != null) sJobs.removeCallbacks(old);
        }
        sJobs.postDelayed(run, delay);
    }
    public static void cancelJob(int id) {
        Runnable old;
        synchronized (sPendingJobs) { old = sPendingJobs.remove(id); }
        if (old != null && sJobs != null) sJobs.removeCallbacks(old);
    }
    /* WorkManager's job ids change each time it schedules a work; the work's own id is in the extras */
    private static String jobKey(android.app.job.JobInfo j) {
        android.os.PersistableBundle e = j.getExtras();
        String w = e != null ? e.getString("EXTRA_WORK_SPEC_ID") : null;
        return w != null ? "work:" + w : j.getService().getClassName() + "#" + j.getId();
    }
    /** As soon as it can, on the main thread (main line's call; the scheduler's jobs go through scheduleJob). */
    public static void runJob(android.app.job.JobInfo j) {
        if (j == null || j.getService() == null) return;
        new Handler(Looper.getMainLooper()).post(() -> startJob(j));
    }
    private static void startJob(android.app.job.JobInfo j) {
        final android.content.ComponentName cn = j.getService();
        {
            try {
                Manifest.Component c = null;
                for (Manifest.Component s : Manifest.services) if (s.name.equals(cn.getClassName())) { c = s; break; }
                if (c == null) { c = new Manifest.Component(); c.name = cn.getClassName(); c.kind = "service"; c.enabled = true; }
                Service s = create(c);
                if (s instanceof android.app.job.JobService) {
                    android.app.job.JobParameters p = new android.app.job.JobParameters();
                    p.huskJobId = j.getId();
                    p.huskExtras = j.getExtras();
                    ((android.app.job.JobService) s).onStartJob(p);
                } else s.onStartCommand(new Intent().setComponent(cn), 0, 1);
            } catch (Throwable t) { android.util.Log.e("Husk", "job " + cn.getClassName() + " failed", t); }
        }
    }

    /** A system service by name, or null for those Husk has nothing for. */
    public static Object system(String name, Context c) {
        switch (name) {
        case Context.AUDIO_SERVICE: return new android.media.AudioManager();
        case Context.VIBRATOR_SERVICE: return new android.os.Vibrator();
        case Context.VIBRATOR_MANAGER_SERVICE: return new android.os.VibratorManager();
        case Context.CLIPBOARD_SERVICE: return new ClipboardManager();
        case Context.INPUT_METHOD_SERVICE: return new android.view.inputmethod.InputMethodManager();
        case Context.SENSOR_SERVICE: return new android.hardware.SensorManager();
        case Context.POWER_SERVICE: return new android.os.PowerManager();
        case Context.ACTIVITY_SERVICE: return new android.app.ActivityManager();
        case Context.CONNECTIVITY_SERVICE: return new android.net.ConnectivityManager();
        case Context.NOTIFICATION_SERVICE: return new android.app.NotificationManager();
        case Context.ALARM_SERVICE: return new android.app.AlarmManager();
        case Context.ACCESSIBILITY_SERVICE: return new android.view.accessibility.AccessibilityManager();
        case Context.UI_MODE_SERVICE: return new android.app.UiModeManager();
        case Context.DISPLAY_SERVICE: return new android.hardware.display.DisplayManager();
        case Context.KEYGUARD_SERVICE: return new android.app.KeyguardManager();
        case Context.USER_SERVICE: return new android.os.UserManager();
        case Context.BATTERY_SERVICE: return new android.os.BatteryManager();
        case Context.STORAGE_SERVICE: return new android.os.storage.StorageManager();
        case Context.TELEPHONY_SERVICE: return new android.telephony.TelephonyManager();
        case Context.JOB_SCHEDULER_SERVICE: return new android.app.job.JobScheduler.Impl();
        case Context.APP_OPS_SERVICE: return new android.app.AppOpsManager();
        case Context.INPUT_SERVICE: return new android.hardware.input.InputManager();
        case Context.WIFI_SERVICE: return new android.net.wifi.WifiManager(c, null);
        case Context.LOCATION_SERVICE: return new android.location.LocationManager(c, null);
        default: return null;
        }
    }
}
