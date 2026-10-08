package com.sbro.emucorev

import android.net.Uri
import androidx.compose.material3.Surface
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsSelected
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.data.TrophySoundMode
import com.sbro.emucorev.data.TrophySoundSettings
import com.sbro.emucorev.ui.settings.TrophySoundSettingsSection
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test

class TrophySoundSettingsSectionTest {
    @get:Rule val compose = createComposeRule()
    private val context = InstrumentationRegistry.getInstrumentation().targetContext

    @Test
    fun pickerCancelLeavesModeUnchangedAndOffPreviewIsReachable() {
        var settings by mutableStateOf(TrophySoundSettings())
        var pickerRequests = 0
        var previews = 0
        compose.setContent {
            EmuCoreVTheme {
                Surface {
                    TrophySoundSettingsSection(
                        settings = settings,
                        selectionError = false,
                        onModeSelected = { settings = settings.copy(mode = it) },
                        onChooseAudio = { pickerRequests++ },
                        onPreview = { previews++ }
                    )
                }
            }
        }

        compose.onNodeWithText(context.getString(R.string.settings_trophy_sound_system)).assertIsSelected()
        compose.onNodeWithText(context.getString(R.string.settings_trophy_sound_custom)).performClick()
        compose.runOnIdle {
            assertEquals(1, pickerRequests)
            assertEquals(TrophySoundMode.SYSTEM, settings.mode)
        }
        compose.onNodeWithText(context.getString(R.string.settings_trophy_sound_off)).performClick()
        compose.onNodeWithText(context.getString(R.string.settings_trophy_sound_off)).assertIsSelected()
        compose.onNodeWithText(context.getString(R.string.settings_trophy_sound_preview)).performClick()
        compose.runOnIdle {
            assertEquals(TrophySoundMode.OFF, settings.mode)
            assertEquals(1, previews)
        }
    }

    @Test
    fun existingCustomChoiceCanBeSelectedAndPreviewed() {
        val selectedUri = Uri.parse("content://test/trophy.ogg")
        var settings by mutableStateOf(
            TrophySoundSettings(mode = TrophySoundMode.OFF, customUri = selectedUri, customName = "trophy.ogg")
        )
        var pickerRequests = 0
        var previews = 0
        compose.setContent {
            EmuCoreVTheme {
                Surface {
                    TrophySoundSettingsSection(
                        settings = settings,
                        selectionError = false,
                        onModeSelected = { settings = settings.copy(mode = it) },
                        onChooseAudio = { pickerRequests++ },
                        onPreview = { previews++ }
                    )
                }
            }
        }

        compose.onNodeWithText(context.getString(R.string.settings_trophy_sound_selected, "trophy.ogg")).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.settings_trophy_sound_custom)).performClick()
        compose.onNodeWithText(context.getString(R.string.settings_trophy_sound_preview)).performClick()
        compose.runOnIdle {
            assertEquals(TrophySoundMode.CUSTOM, settings.mode)
            assertEquals(selectedUri, settings.customUri)
            assertEquals(0, pickerRequests)
            assertEquals(1, previews)
        }
    }
}
