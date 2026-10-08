package com.sbro.emucorev

import android.graphics.Bitmap
import androidx.compose.material3.Surface
import androidx.compose.ui.graphics.asAndroidBitmap
import androidx.compose.ui.test.assertIsDisplayed
import androidx.compose.ui.test.assertIsNotEnabled
import androidx.compose.ui.test.captureToImage
import androidx.compose.ui.test.junit4.createComposeRule
import androidx.compose.ui.test.onNodeWithText
import androidx.compose.ui.test.onRoot
import androidx.compose.ui.test.performClick
import androidx.test.platform.app.InstrumentationRegistry
import com.sbro.emucorev.ui.cheats.CheatCatalogCard
import com.sbro.emucorev.ui.cheats.CheatCatalogEntry
import com.sbro.emucorev.ui.cheats.CheatVersionCompatibility
import com.sbro.emucorev.ui.theme.EmuCoreVTheme
import org.junit.Assert.assertEquals
import org.junit.Rule
import org.junit.Test
import java.io.File

class CheatCatalogCardTest {
    @get:Rule val compose = createComposeRule()
    private val context = InstrumentationRegistry.getInstrumentation().targetContext
    private val entry = CheatCatalogEntry(
        "vitacheat-pcse00465-mp", "PCSE00465", "Sword Art Online: Hollow Fragment (MP)",
        "USA", "1.02", "Yohoki", "Multiplayer codes only", 12,
        "https://example.test/pack.psv", "https://example.test/source"
    )

    @Test
    fun matchingCardShowsRegionRevisionAndReplacementAction() {
        var downloads = 0
        compose.setContent {
            EmuCoreVTheme {
                Surface {
                CheatCatalogCard(entry, false, true, CheatVersionCompatibility.MATCH, false) { downloads++ }
                }
            }
        }
        compose.onNodeWithText(context.getString(R.string.cheat_catalog_pack_version, "1.02", "USA")).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.cheat_catalog_version_matches)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.cheat_catalog_installed)).assertDoesNotExist()
        compose.onNodeWithText(context.getString(R.string.cheat_catalog_replace)).performClick()
        compose.runOnIdle { assertEquals(1, downloads) }
        File(context.cacheDir, "cheat-catalog-card.png").outputStream().use { output ->
            compose.onRoot().captureToImage().asAndroidBitmap().compress(Bitmap.CompressFormat.PNG, 100, output)
        }
    }

    @Test
    fun mismatchedRevisionCannotBeDownloaded() {
        compose.setContent {
            EmuCoreVTheme {
                Surface {
                CheatCatalogCard(entry, false, false, CheatVersionCompatibility.MISMATCH, false) {}
                }
            }
        }
        compose.onNodeWithText(context.getString(R.string.cheat_catalog_version_mismatch)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.cheat_catalog_download)).assertIsNotEnabled()
    }

    @Test
    fun unknownRevisionDoesNotClaimCompatibility() {
        compose.setContent {
            EmuCoreVTheme {
                Surface {
                CheatCatalogCard(entry.copy(version = ""), false, false, CheatVersionCompatibility.UNKNOWN, false) {}
                }
            }
        }
        compose.onNodeWithText(context.getString(R.string.cheat_catalog_version_unverified)).assertIsDisplayed()
        compose.onNodeWithText(context.getString(R.string.cheat_catalog_version_matches)).assertDoesNotExist()
    }
}
