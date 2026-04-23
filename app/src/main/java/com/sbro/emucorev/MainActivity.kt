package com.sbro.emucorev

import android.annotation.SuppressLint
import android.content.res.Configuration
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.SystemBarStyle
import androidx.activity.compose.setContent
import androidx.activity.enableEdgeToEdge
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.material3.Surface
import androidx.compose.ui.Modifier
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.navigation.AppNavigation
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import com.sbro.emucorev.ui.theme.ThemeMode

class MainActivity : ComponentActivity() {
    @SuppressLint("UseKtx")
    override fun onCreate(savedInstanceState: Bundle?) {
        val preferences = AppPreferences(this)
        preferences.applyAppLanguage()
        installSplashScreen()
        enableEdgeToEdge(
            statusBarStyle = SystemBarStyle.auto(
                android.graphics.Color.TRANSPARENT,
                android.graphics.Color.TRANSPARENT
            ),
            navigationBarStyle = SystemBarStyle.auto(
                android.graphics.Color.TRANSPARENT,
                android.graphics.Color.TRANSPARENT
            )
        )
        super.onCreate(savedInstanceState)
        window.setBackgroundDrawable(ColorDrawable(resolveWindowBackground(preferences.themeMode)))
        setContent {
            EmuCoreVTheme(themeMode = preferences.themeMode) {
                Surface(
                    modifier = Modifier.fillMaxSize()
                ) {
                    AppNavigation()
                }
            }
        }
    }

    private fun resolveWindowBackground(themeMode: ThemeMode): Int {
        val darkTheme = when (themeMode) {
            ThemeMode.SYSTEM -> {
                val nightModeMask = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
                nightModeMask == Configuration.UI_MODE_NIGHT_YES
            }
            ThemeMode.LIGHT -> false
            ThemeMode.DARK -> true
        }
        return if (darkTheme) 0xFF000000.toInt() else 0xFFF4F7FB.toInt()
    }
}
