package com.sbro.emucorev.ui.settings

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.isDirectory
import kotlin.io.path.readText
import org.junit.Assert.assertTrue
import org.junit.Test

class SettingsTabsUiContractTest {
    @Test
    fun gameMenuIsASectionInsideCustomizationInsteadOfASeparateTab() {
        val root = sourceRoot()
        val screen = root.resolve("ui/settings/SettingsScreen.kt").readText()
        val content = root.resolve("ui/settings/SettingsTabContent.kt").readText()
        val customization = root.resolve("ui/settings/CustomizationTab.kt").readText()

        assertTrue("Game menu must not remain a standalone settings tab", "SettingsTab.GameMenu" !in content)
        assertTrue("Game menu must not remain in the settings tab enum", "GameMenu(R.string.settings_game_menu_tab" !in screen)
        assertTrue("Game menu settings must be rendered by Customization", "GameMenuStyleSection(" in customization)
    }

    @Test
    fun updatesProAndAboutAreTheFinalTabsInThatOrder() {
        val source = sourceRoot()
            .resolve("ui/settings/SettingsScreen.kt")
            .readText()
        val updates = source.indexOf("Updates(R.string.settings_tab_updates")
        val pro = source.indexOf("Pro(R.string.settings_pro_tab")
        val about = source.indexOf("About(R.string.settings_tab_about")

        assertTrue("Updates tab declaration is missing", updates >= 0)
        assertTrue("Pro tab must follow Updates", pro > updates)
        assertTrue("About tab must follow Pro", about > pro)
        assertTrue(
            "Pro must be directly before About",
            Regex(
                """Updates\(R\.string\.settings_tab_updates[\s\S]*?Pro\(R\.string\.settings_pro_tab[\s\S]*?About\(R\.string\.settings_tab_about"""
            ).containsMatchIn(source)
        )
    }

    @Test
    fun selectedTabIsMadeVisibleAndSmoothlyCentered() {
        val source = sourceRoot()
            .resolve("ui/settings/SettingsScreen.kt")
            .readText()

        assertTrue("Tab row needs persistent lazy-list state", "rememberLazyListState()" in source)
        assertTrue("Tab row must react to selection", "LaunchedEffect(selectedTab)" in source)
        assertTrue("Off-screen tabs must first become visible", "listState.scrollToItem(selectedIndex)" in source)
        assertTrue("Visible selected tabs must animate to center", "listState.animateScrollBy(delta)" in source)
        assertTrue("LazyRow must use the centering state", "state = listState" in source)
    }

    @Test
    fun onboardingUsesSeparateBenefitCardsAndACompactPurchaseCard() {
        val sourceRoot = sourceRoot()
        val onboarding = sourceRoot.resolve("ui/onboarding/OnboardingScreen.kt").readText()
        val proUi = sourceRoot.resolve("ui/pro/ProPurchaseUi.kt").readText()

        assertTrue("Onboarding must render dedicated benefit cards", "ProBenefitCards(" in onboarding)
        assertTrue("Purchase card must not duplicate benefit rows", "showFeatures = false" in onboarding)
        assertTrue("Crimson needs its own card", "settings_pro_feature_crimson_title" in proUi)
        assertTrue("Gold icon needs its own card", "settings_pro_feature_icon_title" in proUi)
        assertTrue("Profile badge needs its own card", "settings_pro_feature_profile_title" in proUi)
        assertTrue("Benefit cards need independent surfaces", "private fun ProBenefitCard(" in proUi)
    }

    @Test
    fun settingsProTabUsesSeparateBenefitCardsAndPurchaseCard() {
        val source = sourceRoot()
            .resolve("ui/settings/SettingsTabContent.kt")
            .readText()
        val proBranch = Regex(
            """SettingsTab\.Pro\s*->\s*Column\([\s\S]*?SettingsTab\.Graphics"""
        ).find(source)?.value.orEmpty()

        assertTrue("The Pro settings tab must use a dedicated column", proBranch.isNotBlank())
        assertTrue("The Pro settings tab must render separate benefit cards", "ProBenefitCards()" in proBranch)
        assertTrue("The purchase card must not duplicate the benefits", "showFeatures = false" in proBranch)
    }

    @Test
    fun proPurchaseButtonHasAVisibleGoldIdentityEverywhere() {
        val proUi = sourceRoot()
            .resolve("ui/pro/ProPurchaseUi.kt")
            .readText()

        assertTrue("Shared Pro purchase button must have a minimum touch height", ".heightIn(min = 56.dp)" in proUi)
        assertTrue("Shared Pro purchase button must use the gold container", "containerColor = ProGold" in proUi)
        assertTrue(
            "Unavailable billing must still retain a visible Pro identity",
            "disabledContainerColor = ProGold.copy(alpha = 0.42f)" in proUi
        )
    }

    private fun sourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        val appModule = sequenceOf(
            workingDirectory,
            workingDirectory.resolve("app")
        ).firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
        return appModule.resolve("src/main/java/com/sbro/emucorev")
    }
}
