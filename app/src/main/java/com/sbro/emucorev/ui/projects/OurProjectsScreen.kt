package com.sbro.emucorev.ui.projects

import android.content.ActivityNotFoundException
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.widget.Toast
import androidx.compose.foundation.BorderStroke
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.BoxWithConstraints
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.FlowRow
import androidx.compose.foundation.layout.IntrinsicSize
import androidx.compose.foundation.layout.PaddingValues
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.navigationBars
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.requiredSize
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.widthIn
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.automirrored.rounded.OpenInNew
import androidx.compose.material.icons.rounded.Code
import androidx.compose.material.icons.rounded.Language
import androidx.compose.material.icons.rounded.PlayArrow
import com.sbro.emucorev.ui.theme.style.Button
import com.sbro.emucorev.ui.theme.style.FilledTonalButton
import androidx.compose.material3.Icon
import com.sbro.emucorev.ui.theme.style.OutlinedButton
import com.sbro.emucorev.ui.theme.style.Surface
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.remember
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Brush
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.platform.LocalContext
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import androidx.compose.ui.unit.sp
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.common.ScreenTopBar
import com.sbro.emucorev.ui.theme.neon.neonShape
import com.sbro.emucorev.ui.theme.neon.neonButtonShape

private fun openLink(context: Context, url: String, failedMessage: String) {
    val intent = Intent(Intent.ACTION_VIEW, Uri.parse(url)).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    try {
        context.startActivity(intent)
    } catch (_: ActivityNotFoundException) {
        Toast.makeText(context, failedMessage, Toast.LENGTH_SHORT).show()
    }
}

private fun openPlay(context: Context, project: OurProject, failedMessage: String) {
    val market = Intent(Intent.ACTION_VIEW, Uri.parse(project.playMarketUrl))
        .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK)
    try {
        context.startActivity(market)
    } catch (_: ActivityNotFoundException) {
        openLink(context, project.playUrl, failedMessage)
    }
}

@Composable
fun OurProjectsScreen(onBackClick: () -> Unit) {
    val context = LocalContext.current
    val failedMessage = stringResource(R.string.projects_open_failed)
    val projects = remember { ourProjects.filter { it.packageName != BuildConfig.APPLICATION_ID } }

    BoxWithConstraints(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
    ) {
        // Text without an explicit color must read on the screen background, not default to black.
        androidx.compose.runtime.CompositionLocalProvider(androidx.compose.material3.LocalContentColor provides MaterialTheme.colorScheme.onBackground) {
        val columns = if (maxWidth >= 720.dp) 2 else 1
        val rows = remember(projects, columns) { projects.chunked(columns) }
        LazyColumn(
            modifier = Modifier.fillMaxSize(),
            contentPadding = PaddingValues(
                start = 12.dp,
                top = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding(),
                end = 12.dp,
                bottom = WindowInsets.navigationBars.asPaddingValues().calculateBottomPadding() + 18.dp
            ),
            verticalArrangement = Arrangement.spacedBy(12.dp),
            horizontalAlignment = Alignment.CenterHorizontally
        ) {
            item {
                ScreenTopBar(
                    title = stringResource(R.string.projects_title),
                    onBackClick = onBackClick,
                    modifier = Modifier
                        .fillMaxWidth()
                        .widthIn(max = 1000.dp)
                        .padding(bottom = 2.dp),
                    titleMaxLines = 2
                )
            }
            items(rows) { row ->
                Row(
                    modifier = Modifier
                        .fillMaxWidth()
                        .widthIn(max = 1000.dp)
                        .height(IntrinsicSize.Max),
                    horizontalArrangement = Arrangement.spacedBy(12.dp)
                ) {
                    row.forEach { project ->
                        ProjectCard(
                            project = project,
                            context = context,
                            failedMessage = failedMessage,
                            modifier = Modifier.weight(1f).fillMaxSize()
                        )
                    }
                    if (row.size < columns) {
                        Spacer(Modifier.weight((columns - row.size).toFloat()))
                    }
                }
            }
            item {
                ProjectsFooter(context = context, failedMessage = failedMessage)
            }
        }
        }
    }
}

@Composable
private fun ProjectIcon(project: OurProject, modifier: Modifier = Modifier) {
    if (project.iconForeground != 0) {
        // The app's real adaptive launcher icon: background + foreground layers (108dp canvas, the
        // launcher shows the centre 72dp), clipped to the theme's corner style.
        Box(modifier = modifier.size(60.dp).clip(neonShape(16.dp)), contentAlignment = Alignment.Center) {
            listOf(project.iconBackground, project.iconForeground).filter { it != 0 }.forEach { layer ->
                androidx.compose.foundation.Image(
                    painter = androidx.compose.ui.res.painterResource(layer),
                    contentDescription = null,
                    modifier = Modifier.requiredSize(90.dp)
                )
            }
        }
        return
    }
    Box(
        modifier = modifier
            .size(60.dp)
            .clip(neonShape(16.dp))
            .background(Brush.linearGradient(listOf(project.gradientStart, project.gradientEnd))),
        contentAlignment = Alignment.Center
    ) {
        Text(
            text = project.monogram,
            color = Color.White,
            fontSize = 28.sp,
            fontWeight = FontWeight.ExtraBold
        )
    }
}

@Composable
private fun Badge(text: String, accent: Color) {
    Box(
        modifier = Modifier
            .clip(neonShape(8.dp))
            .background(accent.copy(alpha = 0.16f))
            .padding(horizontal = 8.dp, vertical = 3.dp)
    ) {
        Text(
            text = text,
            style = MaterialTheme.typography.labelSmall,
            color = accent,
            fontWeight = FontWeight.SemiBold
        )
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ProjectCard(
    project: OurProject,
    context: Context,
    failedMessage: String,
    modifier: Modifier = Modifier
) {
    val shape = neonShape(18.dp)
    Surface(
        modifier = modifier,
        shape = shape,
        color = MaterialTheme.colorScheme.surfaceContainer,
        border = BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.5f))
    ) {
        Column(
            modifier = Modifier.padding(16.dp),
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Row(
                verticalAlignment = Alignment.CenterVertically,
                horizontalArrangement = Arrangement.spacedBy(14.dp)
            ) {
                ProjectIcon(project)
                Column(verticalArrangement = Arrangement.spacedBy(2.dp)) {
                    Text(
                        text = project.name,
                        style = MaterialTheme.typography.titleMedium,
                        fontWeight = FontWeight.Bold,
                        color = MaterialTheme.colorScheme.onSurface
                    )
                    Text(
                        text = project.platform,
                        style = MaterialTheme.typography.bodySmall,
                        color = MaterialTheme.colorScheme.onSurfaceVariant
                    )
                }
            }
            Text(
                text = stringResource(project.descriptionRes),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(6.dp),
                verticalArrangement = Arrangement.spacedBy(6.dp)
            ) {
                if (project.onGooglePlay) {
                    Badge(stringResource(R.string.projects_badge_on_play), MaterialTheme.colorScheme.primary)
                } else {
                    Badge(stringResource(R.string.projects_badge_coming_to_play), MaterialTheme.colorScheme.onSurfaceVariant)
                }
                Badge(stringResource(R.string.projects_badge_open_source), MaterialTheme.colorScheme.onSurfaceVariant)
            }
            FlowRow(
                horizontalArrangement = Arrangement.spacedBy(8.dp),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                if (project.onGooglePlay) {
                    Button(onClick = { openPlay(context, project, failedMessage) }, shape = neonButtonShape()) {
                        Icon(Icons.Rounded.PlayArrow, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.size(6.dp))
                        Text(stringResource(R.string.projects_google_play))
                    }
                }
                FilledTonalButton(onClick = { openLink(context, project.githubUrl, failedMessage) }, shape = neonButtonShape(), colors = androidx.compose.material3.ButtonDefaults.filledTonalButtonColors(containerColor = MaterialTheme.colorScheme.primaryContainer, contentColor = MaterialTheme.colorScheme.onPrimaryContainer)) {
                    Icon(Icons.Rounded.Code, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.size(6.dp))
                    Text(stringResource(R.string.projects_github))
                }
                project.websiteUrl?.let { site ->
                    OutlinedButton(onClick = { openLink(context, site, failedMessage) }, shape = neonButtonShape()) {
                        Icon(Icons.Rounded.Language, contentDescription = null, modifier = Modifier.size(18.dp))
                        Spacer(Modifier.size(6.dp))
                        Text(stringResource(R.string.projects_website))
                    }
                }
            }
        }
    }
}

@OptIn(ExperimentalLayoutApi::class)
@Composable
private fun ProjectsFooter(context: Context, failedMessage: String) {
    Surface(
        modifier = Modifier
            .fillMaxWidth()
            .widthIn(max = 1000.dp)
            .padding(top = 4.dp, bottom = 12.dp),
        shape = neonShape(22.dp),
        color = MaterialTheme.colorScheme.surface,
        border = androidx.compose.foundation.BorderStroke(1.dp, MaterialTheme.colorScheme.outlineVariant.copy(alpha = 0.6f))
    ) {
        Column(
            modifier = Modifier.fillMaxWidth().padding(horizontal = 18.dp, vertical = 16.dp),
            horizontalAlignment = Alignment.CenterHorizontally,
            verticalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            Text(
                text = stringResource(R.string.projects_footer),
                style = MaterialTheme.typography.bodyMedium,
                color = MaterialTheme.colorScheme.onSurfaceVariant,
                textAlign = androidx.compose.ui.text.style.TextAlign.Center,
                modifier = Modifier.fillMaxWidth()
            )
            FlowRow(
                modifier = Modifier.fillMaxWidth(),
                horizontalArrangement = Arrangement.spacedBy(10.dp, Alignment.CenterHorizontally),
                verticalArrangement = Arrangement.spacedBy(8.dp)
            ) {
                FilledTonalButton(
                    onClick = { openLink(context, OUR_PROJECTS_PATREON_URL, failedMessage) },
                    shape = neonButtonShape(),
                    colors = androidx.compose.material3.ButtonDefaults.filledTonalButtonColors(containerColor = MaterialTheme.colorScheme.primaryContainer, contentColor = MaterialTheme.colorScheme.onPrimaryContainer)
                ) {
                    Icon(Icons.AutoMirrored.Rounded.OpenInNew, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.size(8.dp))
                    Text(stringResource(R.string.projects_patreon))
                }
                OutlinedButton(
                    onClick = { openLink(context, OUR_PROJECTS_GITHUB_PROFILE_URL, failedMessage) },
                    shape = neonButtonShape()
                ) {
                    Icon(Icons.Rounded.Code, contentDescription = null, modifier = Modifier.size(18.dp))
                    Spacer(Modifier.size(8.dp))
                    Text(stringResource(R.string.projects_github_profile))
                }
            }
        }
    }
}
