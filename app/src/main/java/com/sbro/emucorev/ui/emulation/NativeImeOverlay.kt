package com.sbro.emucorev.ui.emulation

import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.interaction.MutableInteractionSource
import androidx.compose.foundation.layout.*
import androidx.compose.foundation.rememberScrollState
import androidx.compose.foundation.verticalScroll
import androidx.compose.material3.*
import androidx.compose.runtime.*
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.res.stringResource
import androidx.compose.ui.semantics.contentDescription
import androidx.compose.ui.semantics.semantics
import androidx.compose.ui.text.style.TextOverflow
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.R
import com.sbro.emucorev.core.vita.Emulator
import com.sbro.emucorev.core.vita.NativeImeKeyboard
import java.util.Locale

/** In the activity's existing window: never pauses the guest by opening a Dialog. */
@Composable
internal fun NativeImeOverlay(activity: Emulator) {
    val state = activity.nativeImeState ?: return
    if (!activity.nativeKeyboardRequested || !state.active) return
    var symbols by remember { mutableStateOf(false) }
    var shift by remember { mutableStateOf(false) }
    val builtIn = activity.useBuiltInKeyboard
    Box(
        Modifier.fillMaxSize()
            .background(MaterialTheme.colorScheme.scrim.copy(alpha = 0.2f))
            .clickable(interactionSource = remember { MutableInteractionSource() }, indication = null) { },
        contentAlignment = Alignment.BottomCenter
    ) {
        Surface(
            modifier = Modifier.imePadding().navigationBarsPadding().displayCutoutPadding()
                .padding(8.dp).widthIn(max = 840.dp).fillMaxWidth(),
            shape = MaterialTheme.shapes.large,
            tonalElevation = 6.dp
        ) {
            Column(Modifier.heightIn(max = 320.dp).verticalScroll(rememberScrollState()).padding(8.dp)) {
                Row(verticalAlignment = Alignment.CenterVertically) {
                    Text(
                        text = state.preview,
                        modifier = Modifier.weight(1f).padding(horizontal = 8.dp),
                        style = MaterialTheme.typography.titleMedium,
                        maxLines = if (state.multiline) 2 else 1,
                        overflow = TextOverflow.Ellipsis
                    )
                    TextButton(onClick = { activity.completeNativeIme(cancel = true) }) {
                        Text(stringResource(R.string.common_cancel))
                    }
                    Button(onClick = { activity.completeNativeIme() }) {
                        Text(if (state.dialogActive) stringResource(R.string.emulation_ime_done)
                            else state.enterLabel.ifBlank { stringResource(R.string.emulation_ime_done) })
                    }
                }
                Row(verticalAlignment = Alignment.CenterVertically) {
                    TextButton(onClick = { if (builtIn) activity.requestSystemKeyboard() else activity.showBuiltInKeyboard() }) {
                        Text(stringResource(if (builtIn) R.string.emulation_ime_system else R.string.emulation_ime_builtin))
                    }
                    Spacer(Modifier.weight(1f))
                    ImeKey("←", stringResource(R.string.emulation_ime_left), { activity.editNativeIme("", 2) })
                    ImeKey("→", stringResource(R.string.emulation_ime_right), { activity.editNativeIme("", 3) })
                    ImeKey("⌫", stringResource(R.string.emulation_ime_delete), { activity.editNativeIme("", 1) })
                }
                if (builtIn) {
                    val rows = if (symbols) NativeImeKeyboard.symbolRows else NativeImeKeyboard.latinRows
                    rows.forEach { row ->
                        Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(2.dp)) {
                            row.forEach { key ->
                                val label = if (shift && !symbols) key.toString().uppercase(Locale.ROOT) else key.toString()
                                ImeKey(label, label, { activity.editNativeIme(label, 0) }, Modifier.weight(1f))
                            }
                        }
                    }
                    Row(Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(4.dp)) {
                        ImeKey(if (symbols) "ABC" else "123", if (symbols) "ABC" else "123", { symbols = !symbols })
                        ImeKey(if (shift) "⇩" else "⇧", stringResource(R.string.emulation_ime_shift), { shift = !shift })
                        ImeKey(stringResource(R.string.emulation_ime_space), stringResource(R.string.emulation_ime_space),
                            { activity.editNativeIme(" ", 0) }, Modifier.weight(1f))
                        if (state.multiline) ImeKey("↵", stringResource(R.string.emulation_ime_newline), { activity.editNativeIme("\n", 0) })
                    }
                }
            }
        }
    }
}

@Composable
private fun ImeKey(label: String, description: String, onClick: () -> Unit, modifier: Modifier = Modifier) {
    TextButton(
        onClick = onClick,
        modifier = modifier.heightIn(min = 48.dp).semantics { contentDescription = description },
        contentPadding = PaddingValues(horizontal = 6.dp, vertical = 0.dp)
    ) { Text(label, maxLines = 1) }
}
