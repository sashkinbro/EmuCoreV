package com.sbro.emucorev.ui.library

import com.sbro.emucorev.ui.common.localBitmapLoader

import android.graphics.Bitmap
import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.layout.Box
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.runtime.key
import androidx.compose.runtime.produceState
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.graphics.asImageBitmap
import androidx.compose.ui.layout.ContentScale

@Composable
internal fun LibraryLocalImage(
    path: String?,
    contentDescription: String,
    fallbackLabel: String,
    artworkVersion: Long,
    modifier: Modifier = Modifier
) {
    val bitmap by key(path, artworkVersion) {
        produceState<Bitmap?>(initialValue = localBitmapLoader.cached(path, artworkVersion)) {
            value = localBitmapLoader.load(path, artworkVersion)
        }
    }
    if (bitmap != null) {
        Image(
            bitmap = bitmap!!.asImageBitmap(),
            contentDescription = contentDescription,
            modifier = modifier,
            contentScale = ContentScale.Crop
        )
    } else {
        Box(
            modifier = modifier.background(MaterialTheme.colorScheme.surfaceVariant),
            contentAlignment = Alignment.Center
        ) {
            Text(
                text = fallbackLabel.take(1).uppercase(),
                color = MaterialTheme.colorScheme.onSurfaceVariant
            )
        }
    }
}
