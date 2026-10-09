package com.sbro.emucorev.ui.common

import androidx.compose.runtime.Composable
import androidx.compose.ui.res.stringResource
import com.sbro.emucorev.R

@Composable
fun formatPlayDuration(durationMs: Long): String {
    val totalSeconds = (durationMs / 1_000L).coerceAtLeast(0L)
    val hours = totalSeconds / 3_600L
    val minutes = (totalSeconds % 3_600L) / 60L
    val seconds = totalSeconds % 60L
    return when {
        hours > 0L -> stringResource(R.string.play_time_duration_hours_minutes, hours, minutes)
        minutes > 0L -> stringResource(R.string.play_time_duration_minutes_seconds, minutes, seconds)
        else -> stringResource(R.string.play_time_duration_seconds, seconds)
    }
}
