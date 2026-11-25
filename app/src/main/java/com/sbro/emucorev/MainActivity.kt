package com.sbro.emucorev

import android.annotation.SuppressLint
import android.content.res.Configuration
import android.graphics.drawable.ColorDrawable
import android.os.Bundle
import android.view.KeyEvent
import android.view.MotionEvent
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.fillMaxSize
import com.sbro.emucorev.ui.theme.style.Surface
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.core.splashscreen.SplashScreen.Companion.installSplashScreen
import androidx.lifecycle.Lifecycle
import androidx.lifecycle.lifecycleScope
import androidx.lifecycle.repeatOnLifecycle
import com.sbro.emucorev.core.KeyboardMouseManager
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.CustomizationPreferences
import com.sbro.emucorev.core.review.AndroidInAppReviewAttemptStore
import com.sbro.emucorev.core.review.GooglePlayInAppReviewClient
import com.sbro.emucorev.core.review.InAppReviewCoordinator
import com.sbro.emucorev.core.review.PlayTimeReviewProgressSource
import com.sbro.emucorev.navigation.AppNavigation
import com.sbro.emucorev.ui.common.ImmersiveMode
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import com.sbro.emucorev.ui.theme.ThemeMode
import com.sbro.emucorev.ui.theme.isDark
import com.sbro.emucorev.ui.theme.rememberAppliedThemePackage
import com.sbro.emucorev.data.theme.ThemePackageRepository
import kotlinx.coroutines.Job
import kotlinx.coroutines.delay
import kotlinx.coroutines.launch
import kotlin.time.Duration.Companion.milliseconds

class MainActivity : ComponentActivity() {
    private lateinit var customizationPreferences: CustomizationPreferences
    private lateinit var inAppReviewCoordinator: InAppReviewCoordinator
    private var reviewPromptJob: Job? = null

    @SuppressLint("UseKtx")
    override fun onCreate(savedInstanceState: Bundle?) {
        val preferences = AppPreferences(this)
        customizationPreferences = CustomizationPreferences(this)
        preferences.applyAppLanguage()
        installSplashScreen()
        super.onCreate(savedInstanceState)
        handleNativeLibraryExit(intent)
        KeyboardMouseManager.ensureInitialized(this)
        inAppReviewCoordinator = InAppReviewCoordinator(
            progressSource = PlayTimeReviewProgressSource(applicationContext),
            store = AndroidInAppReviewAttemptStore(applicationContext),
            client = GooglePlayInAppReviewClient(this)
        )
        enterImmersiveMode()
        window.setBackgroundDrawable(ColorDrawable(resolveWindowBackground(preferences.themeMode)))
        lifecycleScope.launch {
            com.sbro.emucorev.core.AgeSignalsController.publishStored(preferences)
            // Once per process, after the first frames, only while resumed (Play may show a consent prompt).
            repeatOnLifecycle(androidx.lifecycle.Lifecycle.State.RESUMED) {
                delay(1500)
                com.sbro.emucorev.core.AgeSignalsController.run(
                    this@MainActivity, preferences, com.sbro.emucorev.BuildConfig.DEBUG
                )
            }
        }

        setContent {
            val customization by customizationPreferences.settings.collectAsState()
            val themeMode by preferences.themeModeFlow.collectAsState(initial = preferences.themeMode)
            val themePackage = rememberAppliedThemePackage(preferences)
            EmuCoreVTheme(
                themeMode = themeMode,
                customization = customization,
                themePackage = themePackage
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

    override fun onNewIntent(intent: android.content.Intent) {
        super.onNewIntent(intent)
        setIntent(intent)
        handleNativeLibraryExit(intent)
    }

    private fun handleNativeLibraryExit(intent: android.content.Intent?) {
        if (intent?.getBooleanExtra(com.sbro.emucorev.core.vita.NativeLibraryMode.EXIT_NATIVE_LIBRARY, false) == true) {
            com.sbro.emucorev.core.vita.NativeLibraryMode.setSelected(this, false)
            intent.removeExtra(com.sbro.emucorev.core.vita.NativeLibraryMode.EXIT_NATIVE_LIBRARY)
        }
    }

    override fun onResume() {
        super.onResume()
        if (com.sbro.emucorev.core.vita.NativeLibraryMode.available(this) &&
            getSharedPreferences("emucorev_library_ui", MODE_PRIVATE).getBoolean("shelf_mode", false)) {
            startActivity(com.sbro.emucorev.core.vita.NativeLibraryMode.intent(this))
            return
        }
        enterImmersiveMode()
        reviewPromptJob?.cancel()
        reviewPromptJob = lifecycleScope.launch {
            delay(REVIEW_PROMPT_DELAY_MS.milliseconds)
            if (lifecycle.currentState.isAtLeast(Lifecycle.State.RESUMED)) {
                inAppReviewCoordinator.attempt()
            }
        }
    }

    override fun onPause() {
        KeyboardMouseManager.cancelBindingCapture()
        reviewPromptJob?.cancel()
        reviewPromptJob = null
        super.onPause()
    }

    @SuppressLint("RestrictedApi")
    override fun dispatchKeyEvent(event: KeyEvent): Boolean {
        if (KeyboardMouseManager.handleKeyBindingCapture(event)) return true
        return super.dispatchKeyEvent(event)
    }

    override fun dispatchGenericMotionEvent(event: MotionEvent): Boolean {
        if (KeyboardMouseManager.handleMouseBindingCapture(event)) return true
        return super.dispatchGenericMotionEvent(event)
    }

    override fun dispatchTouchEvent(event: MotionEvent): Boolean {
        if (KeyboardMouseManager.handleMouseBindingCapture(event)) return true
        return super.dispatchTouchEvent(event)
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
        val nightModeMask = resources.configuration.uiMode and Configuration.UI_MODE_NIGHT_MASK
        val systemDark = nightModeMask == Configuration.UI_MODE_NIGHT_YES
        val appliedPackage = if (themeMode == ThemeMode.PACKAGE) {
            val repository = ThemePackageRepository.getInstance(applicationContext)
            repository.find(AppPreferences(this).themePackageId)
        } else {
            null
        }
        val darkTheme = themeMode.isDark(systemDark, appliedPackage)
        return if (darkTheme) 0xFF000000.toInt() else 0xFFF4F7FB.toInt()
    }

    private companion object {
        const val REVIEW_PROMPT_DELAY_MS = 750L
    }
}
