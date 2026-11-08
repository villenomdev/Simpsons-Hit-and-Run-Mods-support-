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
        ensureStorageDirectories();
    }

    @Override
    protected void onResume() {
        super.onResume();
        ensureStorageDirectories();
    }

    private void ensureStorageDirectories() {
        try {
            File rootDir = new File(Environment.getExternalStorageDirectory(), "Simpsons Hit and Run");
            File filesDir = new File(rootDir, "files");
            File modsDir = new File(rootDir, "mods");

            if (!filesDir.exists()) {
                boolean created = filesDir.mkdirs();
                Log.i(TAG, "Created files directory: " + filesDir.getAbsolutePath() + " (" + created + ")");
            } else {
                Log.i(TAG, "files directory exists: " + filesDir.getAbsolutePath());
            }

            if (!modsDir.exists()) {
                boolean created = modsDir.mkdirs();
                Log.i(TAG, "Created mods directory: " + modsDir.getAbsolutePath() + " (" + created + ")");
            } else {
                Log.i(TAG, "mods directory exists: " + modsDir.getAbsolutePath());
            }

            // Legacy directory backward compatibility
            File legacyModsDir = new File(Environment.getExternalStorageDirectory(), "simpsons_mods");
            if (!legacyModsDir.exists()) {
                legacyModsDir.mkdirs();
            }
        } catch (Exception e) {
            Log.e(TAG, "Failed to create storage directories", e);
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
