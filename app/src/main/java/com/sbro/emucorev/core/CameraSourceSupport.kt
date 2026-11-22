package com.sbro.emucorev.core

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.net.Uri
import java.io.File
import kotlin.math.max

/** A camera of this device that the emulated Vita camera can be fed from. */
data class DeviceCamera(val id: String, val facing: Int) {
    companion object {
        const val FACING_FRONT = 0
        const val FACING_BACK = 1
        const val FACING_EXTERNAL = 2
    }
}

object CameraSourceSupport {
    private const val MAX_IMAGE_EDGE = 1280

    /**
     * Cameras reported by Camera2. The id is the Camera2 id; the native SDL camera driver names
     * its devices "<name> [<camera2 id>]" and the core accepts that bare id in front/back-camera-id.
     */
    fun listCameras(context: Context): List<DeviceCamera> {
        val manager = context.getSystemService(Context.CAMERA_SERVICE) as? CameraManager ?: return emptyList()
        return runCatching {
            manager.cameraIdList.mapNotNull { id ->
                val facing = when (manager.getCameraCharacteristics(id).get(CameraCharacteristics.LENS_FACING)) {
                    CameraCharacteristics.LENS_FACING_FRONT -> DeviceCamera.FACING_FRONT
                    CameraCharacteristics.LENS_FACING_BACK -> DeviceCamera.FACING_BACK
                    CameraCharacteristics.LENS_FACING_EXTERNAL -> DeviceCamera.FACING_EXTERNAL
                    else -> return@mapNotNull null
                }
                DeviceCamera(id, facing)
            }
        }.getOrDefault(emptyList())
    }

    /**
     * Copies the picked image into app storage as a PNG (the core reads it with a plain file path
     * and cannot open content:// URIs) and returns that path, or null if it cannot be decoded.
     */
    fun importImage(context: Context, uri: Uri, slot: String): String? {
        val bitmap = decode(context, uri) ?: return null
        val dir = File(context.filesDir, "camera").apply { mkdirs() }
        val target = File(dir, "$slot.png")
        val temp = File(dir, "$slot.png.tmp")
        return try {
            temp.outputStream().use { bitmap.compress(Bitmap.CompressFormat.PNG, 100, it) }
            if (target.exists() && !target.delete()) return null
            if (temp.renameTo(target)) target.absolutePath else null
        } catch (_: Exception) {
            null
        } finally {
            temp.delete()
            bitmap.recycle()
        }
    }

    private fun decode(context: Context, uri: Uri): Bitmap? = runCatching {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return@runCatching null
        var sample = 1
        while (max(bounds.outWidth, bounds.outHeight) / (sample * 2) >= MAX_IMAGE_EDGE) sample *= 2
        val options = BitmapFactory.Options().apply { inSampleSize = sample }
        context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
    }.getOrNull()
}
