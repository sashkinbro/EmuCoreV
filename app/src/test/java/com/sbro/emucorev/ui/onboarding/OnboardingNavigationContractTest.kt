package com.sbro.emucorev.ui.onboarding

import java.nio.file.Files
import java.nio.file.Path
import kotlin.io.path.readText
import org.junit.Assert.assertTrue
import org.junit.Test

class OnboardingNavigationContractTest {
    @Test
    fun nextAndBackAnimateThePagerFromItsVisiblePage() {
        val source = sourceRoot().resolve("ui/onboarding/OnboardingScreen.kt").readText()
        val navigation = Regex(
            """val goToPage:[\s\S]*?val nextClick = \{ goToPage\(pagerState\.currentPage \+ 1\) \}"""
        ).find(source)?.value.orEmpty()

        assertTrue("Onboarding must use one pager navigation path", navigation.isNotBlank())
        assertTrue("Pager animation must happen directly on click", "pagerState.animateScrollToPage(targetPage)" in navigation)
        assertTrue("The ViewModel page must update after the animation", "viewModel.setCurrentPage(targetPage)" in navigation)
        assertTrue(
            "Button visibility must follow the visible pager page",
            "if (pagerState.currentPage < uiState.totalPages - 1)" in source
        )
    }

    private fun sourceRoot(): Path {
        val workingDirectory = Path.of(System.getProperty("user.dir"))
        val appModule = sequenceOf(workingDirectory, workingDirectory.resolve("app"))
            .firstOrNull { Files.isDirectory(it.resolve("src/main")) }
            ?: error("Unable to locate Android app module from $workingDirectory")
        return appModule.resolve("src/main/java/com/sbro/emucorev")
    }
}
