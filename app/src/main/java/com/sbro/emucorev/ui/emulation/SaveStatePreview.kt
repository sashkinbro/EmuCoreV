package com.sbro.emucorev.ui.emulation

import android.graphics.Bitmap
import android.graphics.BitmapFactory
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.foundation.layout.aspectRatio
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.size
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.rounded.Save
import androidx.compose.material3.Icon
import androidx.compose.material3.MaterialTheme
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.unit.dp
import com.sbro.emucorev.core.SaveStateSlot
import com.sbro.emucorev.ui.theme.neon.neonShape
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.withContext

/** Decode only the selected slot, off the UI thread, with bounded pixel allocation. */
internal fun decodeSaveStatePreview(path: String?): Bitmap? {
    if (path == null) return null
    return try {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        BitmapFactory.decodeFile(path, bounds)
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return null
        var sample = 1
        while (bounds.outWidth / sample > 720 || bounds.outHeight / sample > 408) sample *= 2
        BitmapFactory.decodeFile(path, BitmapFactory.Options().apply {
            inSampleSize = sample
            inPreferredConfig = Bitmap.Config.RGB_565
        })
    } catch (_: OutOfMemoryError) {
        null
    } catch (_: Exception) {
        null
    }
}

@Composable
internal fun SaveStatePreview(slot: SaveStateSlot?, contentDescription: String) {
    val path = slot?.thumbnailPath.takeIf { slot?.exists == true }
    // Include the revision: overwriting a slot keeps its path.
    val bitmap by produceState<Bitmap?>(null, path, slot?.timestamp, slot?.sizeBytes) {
        value = null
        value = withContext(Dispatchers.IO) { decodeSaveStatePreview(path) }
    }
    Box(
        modifier = Modifier.fillMaxWidth().aspectRatio(16f / 9f)
            .clip(neonShape(14.dp)).background(MaterialTheme.colorScheme.surfaceVariant),
        contentAlignment = Alignment.Center
    ) {
        val preview = bitmap
        if (preview != null) {
            Image(preview.asImageBitmap(), contentDescription, Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
        } else {
            Icon(Icons.Rounded.Save, contentDescription = null,
                tint = MaterialTheme.colorScheme.onSurfaceVariant.copy(alpha = 0.26f), modifier = Modifier.size(42.dp))
        }
    }
}
