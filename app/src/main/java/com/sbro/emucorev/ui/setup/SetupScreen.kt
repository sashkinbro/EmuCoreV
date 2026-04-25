package com.sbro.emucorev.ui.setup

import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.ExperimentalLayoutApi
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.WindowInsets
import androidx.compose.foundation.layout.asPaddingValues
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.navigationBarsPadding
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.layout.statusBarsIgnoringVisibility
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.shape.RoundedCornerShape
import androidx.compose.foundation.verticalScroll
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Inventory2
import androidx.compose.material.icons.rounded.Key
import androidx.compose.material.icons.rounded.Storage
import androidx.compose.material3.Button
import androidx.compose.material3.FilledTonalButton
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.OutlinedTextField
import androidx.compose.material3.Surface
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.saveable.rememberSaveable
import androidx.compose.runtime.setValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.vector.ImageVector
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.ui.common.NavigationBackButton
import com.sbro.emucorev.ui.common.SectionCard
import com.sbro.emucorev.ui.theme.ScreenContentBottomPadding
import com.sbro.emucorev.ui.theme.ScreenHorizontalPadding
import com.sbro.emucorev.ui.theme.ScreenTopInsetOffset

@OptIn(ExperimentalLayoutApi::class)
@Suppress("UNUSED_PARAMETER")
@Composable
fun SetupScreen(
    packagesFolderLabel: String?,
    vitaRootPath: String,
    onBackClick: () -> Unit,
    onInstallFirmware: () -> Unit,
    onInstallContent: () -> Unit,
    onInstallPkg: (String) -> Unit
) {
    val topInset = WindowInsets.statusBarsIgnoringVisibility.asPaddingValues().calculateTopPadding() + ScreenTopInsetOffset
    var zrif by rememberSaveable { mutableStateOf("") }

    Column(
        modifier = Modifier
            .fillMaxSize()
            .background(MaterialTheme.colorScheme.background)
            .navigationBarsPadding()
            .verticalScroll(rememberScrollState())
            .padding(
                start = ScreenHorizontalPadding,
                end = ScreenHorizontalPadding,
                top = topInset,
                bottom = ScreenContentBottomPadding
            ),
        verticalArrangement = Arrangement.spacedBy(18.dp)
    ) {
        Row(
            modifier = Modifier.fillMaxWidth(),
            verticalAlignment = Alignment.CenterVertically,
            horizontalArrangement = Arrangement.spacedBy(12.dp)
        ) {
            NavigationBackButton(onClick = onBackClick)
            Column(modifier = Modifier.weight(1f)) {
                Text(
                    text = stringResource(R.string.setup_title),
                    style = MaterialTheme.typography.headlineMedium.copy(fontWeight = FontWeight.Bold),
                    color = MaterialTheme.colorScheme.onBackground,
                    maxLines = 2,
                    overflow = TextOverflow.Ellipsis
                )
            }
        }

        SetupInfoCard(
            icon = Icons.Rounded.Storage,
            title = stringResource(R.string.onboarding_storage_title),
            body = vitaRootPath
        )

        SetupActionCard(
            icon = Icons.Rounded.Inventory2,
            title = stringResource(R.string.setup_content_title),
            body = stringResource(R.string.setup_content_body),
            buttonLabel = stringResource(R.string.setup_content_button),
            onClick = onInstallContent
        )

        SectionCard(title = stringResource(R.string.setup_pkg_title)) {
            Column(verticalArrangement = Arrangement.spacedBy(12.dp)) {
                SetupInfoRow(
                    icon = Icons.Rounded.Key,
                    text = stringResource(R.string.setup_pkg_body)
                )
                OutlinedTextField(
                    value = zrif,
                    onValueChange = { zrif = it },
                    modifier = Modifier.fillMaxWidth(),
                    label = { Text(stringResource(R.string.setup_pkg_zrif_label)) },
                    placeholder = { Text(stringResource(R.string.setup_pkg_zrif_placeholder)) },
                    minLines = 3,
                    shape = RoundedCornerShape(22.dp)
                )
                FilledTonalButton(
                    onClick = { onInstallPkg(zrif.trim()) },
                    modifier = Modifier.fillMaxWidth()
                ) {
                    Text(stringResource(R.string.setup_pkg_button))
                }
            }
        }
    }
}

@Composable
private fun SetupInfoCard(
    icon: ImageVector,
    title: String,
    body: String
) {
    SectionCard(title = title) {
        SetupInfoRow(icon = icon, text = body)
    }
}

@Composable
private fun SetupInfoRow(
    icon: ImageVector,
    text: String
) {
    Row(
        horizontalArrangement = Arrangement.spacedBy(12.dp),
        verticalAlignment = Alignment.Top
    ) {
        Surface(
            shape = RoundedCornerShape(16.dp),
            color = MaterialTheme.colorScheme.primary.copy(alpha = 0.12f)
        ) {
            Box(
                modifier = Modifier.padding(12.dp),
                contentAlignment = Alignment.Center
            ) {
                Icon(
                    imageVector = icon,
                    contentDescription = null,
                    tint = MaterialTheme.colorScheme.primary,
                    modifier = Modifier.size(22.dp)
                )
            }
        }
        Text(
            text = text,
            style = MaterialTheme.typography.bodyMedium,
            color = MaterialTheme.colorScheme.onSurfaceVariant,
            modifier = Modifier.weight(1f)
        )
    }
}

@Composable
private fun SetupActionCard(
    icon: ImageVector,
    title: String,
    body: String,
    buttonLabel: String,
    onClick: () -> Unit
) {
    SectionCard(title = title) {
        Column(verticalArrangement = Arrangement.spacedBy(14.dp)) {
            SetupInfoRow(icon = icon, text = body)
            Button(
                onClick = onClick,
                modifier = Modifier.fillMaxWidth()
            ) {
                Text(buttonLabel)
            }
        }
    }
}
