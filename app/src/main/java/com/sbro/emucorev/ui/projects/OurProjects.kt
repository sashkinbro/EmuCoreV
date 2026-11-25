package com.sbro.emucorev.ui.projects

import androidx.annotation.DrawableRes
import androidx.annotation.StringRes
import androidx.compose.ui.graphics.Color
import com.sbro.emucorev.R

/**
 * Catalogue of the EmuCore family apps. The screen hides the entry whose [packageName] equals the
 * running application id, so this file and OurProjectsScreen.kt can be copied unchanged into
 * every sibling app.
 */
data class OurProject(
    val packageName: String,
    val name: String,
    val platform: String,
    @StringRes val descriptionRes: Int,
    val githubUrl: String,
    val onGooglePlay: Boolean,
    val websiteUrl: String? = null,
    val gradientStart: Color,
    val gradientEnd: Color,
    @DrawableRes val iconForeground: Int = 0,
    @DrawableRes val iconBackground: Int = 0
) {
    val monogram: String get() = name.removePrefix("EmuCore").take(1)
    val playUrl: String get() = "https://play.google.com/store/apps/details?id=$packageName"
    val playMarketUrl: String get() = "market://details?id=$packageName"
}

const val OUR_PROJECTS_PATREON_URL = "https://www.patreon.com/c/emucore"
const val OUR_PROJECTS_GITHUB_PROFILE_URL = "https://github.com/sashkinbro"

/** Ordered by importance: X, V, R, A, H, D, C (the running app is filtered out). */
val ourProjects: List<OurProject> = listOf(
    OurProject(
        packageName = "com.sbro.emucorev",
        name = "EmuCoreX",
        platform = "PlayStation 2",
        descriptionRes = R.string.projects_desc_x,
        githubUrl = "https://github.com/sashkinbro/EmuCoreX",
        onGooglePlay = true,
        websiteUrl = "https://emucorex.web.app",
        gradientStart = Color(0xFF7C4DFF),
        gradientEnd = Color(0xFF2962FF),
        iconForeground = R.drawable.project_icon_x_fo,
        iconBackground = R.drawable.project_icon_x_ba
    ),
    OurProject(
        packageName = "com.sbro.emucorev",
        name = "EmuCoreV",
        platform = "PlayStation Vita",
        descriptionRes = R.string.projects_desc_v,
        githubUrl = "https://github.com/sashkinbro/EmuCoreV",
        onGooglePlay = true,
        websiteUrl = "https://emucorev.web.app",
        gradientStart = Color(0xFF00C853),
        gradientEnd = Color(0xFF0277BD),
        iconForeground = R.drawable.project_icon_v_fo,
        iconBackground = R.drawable.project_icon_v_ba
    ),
    OurProject(
        packageName = "com.sbro.emucorer",
        name = "EmuCoreR",
        platform = "PlayStation 1",
        descriptionRes = R.string.projects_desc_r,
        githubUrl = "https://github.com/sashkinbro/EmuCoreR",
        onGooglePlay = true,
        websiteUrl = "https://emucorer.web.app",
        gradientStart = Color(0xFF9E9E9E),
        gradientEnd = Color(0xFF37474F),
        iconForeground = R.drawable.project_icon_r_fo,
        iconBackground = R.drawable.project_icon_r_ba
    ),
    OurProject(
        packageName = "com.sbro.emucorea",
        name = "EmuCoreA",
        platform = "PSP",
        descriptionRes = R.string.projects_desc_a,
        githubUrl = "https://github.com/sashkinbro/EmuCoreA",
        onGooglePlay = true,
        websiteUrl = "https://emucorea.web.app",
        gradientStart = Color(0xFF00B0FF),
        gradientEnd = Color(0xFF00695C),
        iconForeground = R.drawable.project_icon_a_fo,
        iconBackground = R.drawable.project_icon_a_ba
    ),
    OurProject(
        packageName = "com.sbro.emucoreh",
        name = "EmuCoreH",
        platform = "Sega",
        descriptionRes = R.string.projects_desc_h,
        githubUrl = "https://github.com/sashkinbro/EmuCoreH",
        onGooglePlay = true,
        websiteUrl = "https://emucoreh.web.app",
        gradientStart = Color(0xFF1565C0),
        gradientEnd = Color(0xFF00B8D4),
        iconForeground = R.drawable.project_icon_h_fo,
        iconBackground = R.drawable.project_icon_h_ba
    ),
    OurProject(
        packageName = "com.sbro.emucored",
        name = "EmuCoreD",
        platform = "Nintendo",
        descriptionRes = R.string.projects_desc_d,
        githubUrl = "https://github.com/sashkinbro/EmuCoreD",
        onGooglePlay = false,
        websiteUrl = "https://emucored.web.app",
        gradientStart = Color(0xFFFF5252),
        gradientEnd = Color(0xFFC2185B),
        iconForeground = R.drawable.project_icon_d_fo,
        iconBackground = R.drawable.project_icon_d_ba
    ),
    OurProject(
        packageName = "com.sbro.emucorec",
        name = "EmuCoreC",
        platform = "PlayStation 3",
        descriptionRes = R.string.projects_desc_c,
        githubUrl = "https://github.com/sashkinbro/EmuCoreC",
        onGooglePlay = false,
        websiteUrl = "https://emucorec.web.app",
        gradientStart = Color(0xFF455A64),
        gradientEnd = Color(0xFF1E88E5),
        iconForeground = R.drawable.project_icon_c_fo,
        iconBackground = R.drawable.project_icon_c_ba
    )
)
