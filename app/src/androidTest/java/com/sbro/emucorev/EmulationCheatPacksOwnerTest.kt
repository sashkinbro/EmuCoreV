package com.sbro.emucorev

import android.app.Application
import androidx.compose.runtime.CompositionLocalProvider
import androidx.compose.runtime.SideEffect
import androidx.compose.runtime.mutableStateOf
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.lifecycle.ViewModelStore
import androidx.lifecycle.ViewModelStoreOwner
import androidx.lifecycle.viewmodel.compose.LocalViewModelStoreOwner
import androidx.test.core.app.ApplicationProvider
import com.sbro.emucorev.ui.emulation.EmulationCheatPacksViewModel
import com.sbro.emucorev.ui.emulation.rememberEmulationCheatPacksViewModel
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertSame
import org.junit.Rule
import org.junit.Test

class EmulationCheatPacksOwnerTest {
    @get:Rule val compose = createComposeRule()

    @Test fun bareSdlOwnerCreatesAndroidModelAndRetainsItAcrossMenuDismissal() {
        // Matches Emulator.ComposeOwners: intentionally no default factory or
        // Application CreationExtras supplied by ComponentActivity.
        val owner = object : ViewModelStoreOwner {
            override val viewModelStore = ViewModelStore()
        }
        val visible = mutableStateOf(true)
        var current: EmulationCheatPacksViewModel? = null
        compose.setContent {
            CompositionLocalProvider(LocalViewModelStoreOwner provides owner) {
                if (visible.value) {
                    val model = rememberEmulationCheatPacksViewModel()
                    SideEffect { current = model }
                }
            }
        }
        try {
            compose.waitForIdle()
            val original = current
            compose.runOnIdle {
                assertNotNull(original)
                assertSame(ApplicationProvider.getApplicationContext<Application>(), original!!.getApplication<Application>())
                visible.value = false
                current = null
            }
            compose.waitForIdle()
            compose.runOnIdle { visible.value = true }
            compose.waitForIdle()
            compose.runOnIdle { assertSame(original, current) }
        } finally {
            compose.runOnIdle { owner.viewModelStore.clear() }
        }
    }
}
