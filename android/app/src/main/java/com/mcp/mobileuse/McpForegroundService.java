package com.mcp.mobileuse;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Intent;
import android.os.IBinder;
import android.util.Log;

public class McpForegroundService extends Service {
    private static final String TAG = "McpForegroundService";
    private static final String CHANNEL_ID = "mcp_server";
    private static final int NOTIFICATION_ID = 1;

    public static final String EXTRA_PORT = "port";
    public static final String EXTRA_BACKEND = "backend";

    private boolean running = false;

    @Override
    public IBinder onBind(Intent intent) {
        return null;
    }

    @Override
    public int onStartCommand(Intent intent, int flags, int startId) {
        int port = intent != null ? intent.getIntExtra(EXTRA_PORT, 8080) : 8080;
        String backend = intent != null ? intent.getStringExtra(EXTRA_BACKEND) : "adb";
        if (backend == null) backend = "adb";

        startForeground(NOTIFICATION_ID, buildNotification(port, backend));

        if (!running) {
            int rc = JniBridge.nativeStart(port, backend);
            if (rc == 0) {
                running = true;
                Log.i(TAG, "mcp_mobile_use server started on port " + port);
            } else {
                Log.e(TAG, "nativeStart failed: " + rc);
                stopSelf();
            }
        }
        return START_STICKY;
    }

    @Override
    public void onDestroy() {
        if (running) {
            JniBridge.nativeStop();
            running = false;
        }
        super.onDestroy();
    }

    private Notification buildNotification(int port, String backend) {
        NotificationManager nm = getSystemService(NotificationManager.class);
        if (nm != null && nm.getNotificationChannel(CHANNEL_ID) == null) {
            nm.createNotificationChannel(new NotificationChannel(
                    CHANNEL_ID, getString(R.string.channel_name),
                    NotificationManager.IMPORTANCE_LOW));
        }
        PendingIntent pi = PendingIntent.getActivity(
                this, 0, new Intent(this, MainActivity.class),
                PendingIntent.FLAG_IMMUTABLE);
        return new Notification.Builder(this, CHANNEL_ID)
                .setContentTitle(getString(R.string.app_name))
                .setContentText("MCP server on :" + port + " (" + backend + ")")
                .setContentIntent(pi)
                .setOngoing(true)
                .build();
    }
}
