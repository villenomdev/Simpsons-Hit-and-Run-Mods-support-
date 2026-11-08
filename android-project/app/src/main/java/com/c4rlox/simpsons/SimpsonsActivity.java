package com.c4rlox.simpsons;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;

public class SimpsonsActivity extends SDLActivity {
    private static final String TAG = "SimpsonsHitAndRun";
    private static final int STORAGE_PERMISSION_CODE = 1001;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        checkAndRequestStoragePermissions();
        ensureModsDirectory();
    }

    private void ensureModsDirectory() {
        try {
            File modsDir = new File(Environment.getExternalStorageDirectory(), "simpsons_mods");
            if (!modsDir.exists()) {
                boolean created = modsDir.mkdirs();
                Log.i(TAG, "Created simpsons_mods directory: " + modsDir.getAbsolutePath() + " (" + created + ")");
            } else {
                Log.i(TAG, "simpsons_mods directory exists: " + modsDir.getAbsolutePath());
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to create simpsons_mods directory", e);
        }
    }

    private void checkAndRequestStoragePermissions() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            // Android 11+ (API 30+) All Files Access
            if (!Environment.isExternalStorageManager()) {
                try {
                    Intent intent = new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION);
                    intent.addCategory("android.intent.category.DEFAULT");
                    intent.setData(Uri.parse(String.format("package:%s", getPackageName())));
                    startActivity(intent);
                } catch (Exception e) {
                    try {
                        Intent intent = new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION);
                        startActivity(intent);
                    } catch (Exception ex) {
                        Log.e(TAG, "Unable to request MANAGE_ALL_FILES_ACCESS_PERMISSION", ex);
                    }
                }
            }
        } else if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.M) {
            // Android 6.0 - 10
            if (checkSelfPermission(Manifest.permission.READ_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED ||
                checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
                requestPermissions(new String[]{
                    Manifest.permission.READ_EXTERNAL_STORAGE,
                    Manifest.permission.WRITE_EXTERNAL_STORAGE
                }, STORAGE_PERMISSION_CODE);
            }
        }
    }
}
