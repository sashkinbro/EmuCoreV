package com.sbro.emucorev

import android.annotation.SuppressLint
import android.content.res.Configuration
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.CustomizationPreferences
import com.sbro.emucorev.navigation.AppNavigation
import com.sbro.emucorev.ui.common.ImmersiveMode
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import com.sbro.emucorev.ui.theme.ThemeMode

class MainActivity : ComponentActivity() {
    private lateinit var customizationPreferences: CustomizationPreferences

    @SuppressLint("UseKtx")
    override fun onCreate(savedInstanceState: Bundle?) {
        val preferences = AppPreferences(this)
        customizationPreferences = CustomizationPreferences(this)
        preferences.applyAppLanguage()
        installSplashScreen()
        super.onCreate(savedInstanceState)
        enterImmersiveMode()
        window.setBackgroundDrawable(ColorDrawable(resolveWindowBackground(preferences.themeMode)))
        setContent {
            val customization by customizationPreferences.settings.collectAsState()
            val themeMode by preferences.themeModeFlow.collectAsState(initial = preferences.themeMode)
            EmuCoreVTheme(
                themeMode = themeMode,
                customization = customization
            ) {
                Surface(
                    modifier = Modifier.fillMaxSize()
                ) {
                    AppNavigation()
                }
            }
        }
        window.decorView.post(::enterImmersiveMode)
    }

    override fun onDestroy() {
        if (::customizationPreferences.isInitialized) {
            customizationPreferences.close()
        }
        super.onDestroy()
    }

    override fun onResume() {
        super.onResume()
        enterImmersiveMode()
    }

    override fun onWindowFocusChanged(hasFocus: Boolean) {
        super.onWindowFocusChanged(hasFocus)
        if (hasFocus) {
            enterImmersiveMode()
        }
    }

    private fun enterImmersiveMode() {
        ImmersiveMode.apply(window)
    }

    private fun resolveWindowBackground(themeMode: ThemeMode): Int {
        val darkTheme = when (themeMode) {
            ThemeMode.SYSTEM -> {
                val nightModeMask = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
                nightModeMask == Configuration.UI_MODE_NIGHT_YES
            }
            ThemeMode.LIGHT -> false
            ThemeMode.DARK -> true
            ThemeMode.PRO -> true
        }
        return if (darkTheme) 0xFF000000.toInt() else 0xFFF4F7FB.toInt()
    }
}
