package com.sbro.emucorev.core

import android.content.Context
import android.graphics.Bitmap
import android.graphics.BitmapFactory
import android.hardware.camera2.CameraCharacteristics
import android.hardware.camera2.CameraManager
import android.media.ExifInterface
import android.Manifest
import android.content.pm.PackageManager
import androidx.core.content.ContextCompat
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

/** Clockwise rotation, then an optional horizontal mirror: the EXIF orientation as a pixel transform. */
data class ExifTransform(val rotationDegrees: Int, val mirrorHorizontal: Boolean) {
    val isIdentity: Boolean get() = rotationDegrees == 0 && !mirrorHorizontal
}

/** Pixels (ARGB ints, row-major) with their size. */
class PixelImage(val pixels: IntArray, val width: Int, val height: Int)

/** Standard EXIF orientation values 1..8 (unknown values mean "upright"). */
fun exifTransform(orientation: Int): ExifTransform = when (orientation) {
    2 -> ExifTransform(0, true)
    3 -> ExifTransform(180, false)
    4 -> ExifTransform(180, true)
    5 -> ExifTransform(90, true)
    6 -> ExifTransform(90, false)
    7 -> ExifTransform(270, true)
    8 -> ExifTransform(270, false)
    else -> ExifTransform(0, false)
}

/** Applies [transform] to the pixels; the source is returned as is for the identity. */
fun applyExifTransform(source: PixelImage, transform: ExifTransform): PixelImage {
    if (transform.isIdentity) return source
    val w = source.width
    val h = source.height
    val swap = transform.rotationDegrees == 90 || transform.rotationDegrees == 270
    val dw = if (swap) h else w
    val dh = if (swap) w else h
    val out = IntArray(source.pixels.size)
    for (y in 0 until h) {
        for (x in 0 until w) {
            var dx: Int
            val dy: Int
            when (transform.rotationDegrees) {
                90 -> { dx = h - 1 - y; dy = x }
                180 -> { dx = w - 1 - x; dy = h - 1 - y }
                270 -> { dx = y; dy = w - 1 - x }
                else -> { dx = x; dy = y }
            }
            if (transform.mirrorHorizontal) dx = dw - 1 - dx
            out[dy * dw + dx] = source.pixels[y * w + x]
        }
    }
    return PixelImage(out, dw, dh)
}

/**
 * A stored image path that no longer exists (new device, restored backup) must not leave the camera
 * on a source that cannot work: such an Image source falls back to the default real camera (which in
 * turn falls back to a dark frame on devices without one) and a stale path is cleared.
 */
fun validCameraSource(type: Int, imagePath: String, exists: (String) -> Boolean): Pair<Int, String> {
    val usable = imagePath.isNotBlank() && exists(imagePath)
    return when {
        type == VitaCoreConfig.CAMERA_SOURCE_IMAGE && !usable -> VitaCoreConfig.CAMERA_SOURCE_REAL to ""
        !usable -> type to ""
        else -> type to imagePath
    }
}

internal fun VitaCoreConfig.withValidCameraSources(exists: (String) -> Boolean = { File(it).isFile }): VitaCoreConfig {
    val front = validCameraSource(frontCameraType, frontCameraImage, exists)
    val back = validCameraSource(backCameraType, backCameraImage, exists)
    return copy(
        frontCameraType = front.first, frontCameraImage = front.second,
        backCameraType = back.first, backCameraImage = back.second
    )
}

object CameraSourceSupport {
    private const val MAX_IMAGE_EDGE = 1280

    fun hasPermission(context: Context): Boolean =
        ContextCompat.checkSelfPermission(context, Manifest.permission.CAMERA) == PackageManager.PERMISSION_GRANTED

    /** True when the stored image file is gone (the Image source would show a dark frame). */
    fun isImageMissing(path: String): Boolean = path.isNotBlank() && !File(path).isFile

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

    /** EXIF orientation of the picked image (1 = upright when absent or unreadable). */
    private fun readOrientation(context: Context, uri: Uri): Int = runCatching {
        context.contentResolver.openInputStream(uri)?.use {
            ExifInterface(it).getAttributeInt(ExifInterface.TAG_ORIENTATION, ExifInterface.ORIENTATION_NORMAL)
        }
    }.getOrNull() ?: ExifInterface.ORIENTATION_NORMAL

    /** Upright copy of [bitmap] according to the EXIF [orientation] (the same instance when upright). */
    private fun orient(bitmap: Bitmap, orientation: Int): Bitmap {
        val transform = exifTransform(orientation)
        if (transform.isIdentity) return bitmap
        val pixels = IntArray(bitmap.width * bitmap.height)
        bitmap.getPixels(pixels, 0, bitmap.width, 0, 0, bitmap.width, bitmap.height)
        val result = applyExifTransform(PixelImage(pixels, bitmap.width, bitmap.height), transform)
        val oriented = Bitmap.createBitmap(result.pixels, result.width, result.height, Bitmap.Config.ARGB_8888)
        bitmap.recycle()
        return oriented
    }

    private fun decode(context: Context, uri: Uri): Bitmap? = runCatching {
        val bounds = BitmapFactory.Options().apply { inJustDecodeBounds = true }
        context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, bounds) }
        if (bounds.outWidth <= 0 || bounds.outHeight <= 0) return@runCatching null
        var sample = 1
        while (max(bounds.outWidth, bounds.outHeight) / (sample * 2) >= MAX_IMAGE_EDGE) sample *= 2
        val options = BitmapFactory.Options().apply { inSampleSize = sample }
        val decoded = context.contentResolver.openInputStream(uri)?.use { BitmapFactory.decodeStream(it, null, options) }
        // Phone photos store the sensor orientation in EXIF: bake it into the pixels, the core reads a plain PNG.
        decoded?.let { orient(it, readOrientation(context, uri)) }
    }.getOrNull()
}
