package com.sbro.emucorev.nativeuipreview;

import android.app.NativeActivity;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;

/** Independent development surface: loads only libpaf_preview. */
public final class PreviewActivity extends NativeActivity {
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        WindowManager.LayoutParams attributes = getWindow().getAttributes();
        attributes.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_NEVER;
        getWindow().setAttributes(attributes);
        getWindow().getDecorView().setSystemUiVisibility(
            View.SYSTEM_UI_FLAG_FULLSCREEN | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY | View.SYSTEM_UI_FLAG_LAYOUT_STABLE);
    }
}
