package com.sbro.emucorev.core

import android.content.Context
import java.io.File
import org.junit.After
import org.junit.Assert.assertArrayEquals
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertSame
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

/** Camera image import (EXIF, stale sources), microphone settings and permission decision logic. */
@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class CameraMicSettingsTest {
    private lateinit var context: Context
    private val titleId = "PCSE00000"

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        AppStartup.awaitReady()
        File(EmulatorStorage.runtimeRoot(context), "config").deleteRecursively()
        File(EmulatorStorage.runtimeRoot(context), "config.yml").delete()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @After
    fun tearDown() {
        File(EmulatorStorage.runtimeRoot(context), "config").deleteRecursively()
        File(EmulatorStorage.runtimeRoot(context), "config.yml").delete()
        context.getSharedPreferences("emucorev_ui_config", Context.MODE_PRIVATE).edit().clear().commit()
    }

    // ---- EXIF ----

    @Test
    fun exifOrientationSixRotatesAPortraitPhotoUpright() {
        // A 2x1 sensor image [A, B] tagged orientation 6 (rotate 90 clockwise) is a 1x2 image: A over B.
        val a = 0xFFFF0000.toInt()
        val b = 0xFF0000FF.toInt()
        val result = applyExifTransform(PixelImage(intArrayOf(a, b), 2, 1), exifTransform(6))
        assertEquals(1, result.width)
        assertEquals(2, result.height)
        assertArrayEquals(intArrayOf(a, b), result.pixels)
    }

    @Test
    fun exifOrientationEightRotatesTheOtherWay() {
        val a = 1
        val b = 2
        val result = applyExifTransform(PixelImage(intArrayOf(a, b), 2, 1), exifTransform(8))
        assertEquals(1, result.width)
        assertArrayEquals(intArrayOf(b, a), result.pixels)
    }

    @Test
    fun exifMirrorAndHalfTurnOrientations() {
        val src = PixelImage(intArrayOf(1, 2, 3, 4), 2, 2) // [[1,2],[3,4]]
        assertArrayEquals(intArrayOf(2, 1, 4, 3), applyExifTransform(src, exifTransform(2)).pixels)
        assertArrayEquals(intArrayOf(4, 3, 2, 1), applyExifTransform(src, exifTransform(3)).pixels)
        assertArrayEquals(intArrayOf(3, 4, 1, 2), applyExifTransform(src, exifTransform(4)).pixels)
        // Transpose: rows become columns.
        assertArrayEquals(intArrayOf(1, 3, 2, 4), applyExifTransform(src, exifTransform(5)).pixels)
        assertArrayEquals(intArrayOf(4, 2, 3, 1), applyExifTransform(src, exifTransform(7)).pixels)
    }

    @Test
    fun uprightOrUnknownOrientationLeavesThePixelsAlone() {
        val src = PixelImage(intArrayOf(1, 2), 2, 1)
        assertSame(src, applyExifTransform(src, exifTransform(1)))
        assertSame(src, applyExifTransform(src, exifTransform(0)))
        assertSame(src, applyExifTransform(src, exifTransform(99)))
    }

    // ---- stale camera sources ----

    @Test
    fun imageSourceWithoutAFileFallsBackToTheRealCamera() {
        val (type, path) = validCameraSource(VitaCoreConfig.CAMERA_SOURCE_IMAGE, "", { true })
        assertEquals(VitaCoreConfig.CAMERA_SOURCE_REAL, type)
        assertEquals("", path)
        val missing = validCameraSource(VitaCoreConfig.CAMERA_SOURCE_IMAGE, "/old/device/front.png", { false })
        assertEquals(VitaCoreConfig.CAMERA_SOURCE_REAL to "", missing)
    }

    @Test
    fun existingImageAndOtherSourcesAreKept() {
        assertEquals(
            VitaCoreConfig.CAMERA_SOURCE_IMAGE to "/files/camera/front.png",
            validCameraSource(VitaCoreConfig.CAMERA_SOURCE_IMAGE, "/files/camera/front.png", { true })
        )
        assertEquals(VitaCoreConfig.CAMERA_SOURCE_SOLID to "", validCameraSource(VitaCoreConfig.CAMERA_SOURCE_SOLID, "/stale.png", { false }))
    }

    @Test
    fun restoredConfigDropsStaleImagePathsOnBothSlots() {
        val config = VitaCoreConfig(
            frontCameraType = VitaCoreConfig.CAMERA_SOURCE_IMAGE, frontCameraImage = "/gone/front.png",
            backCameraType = VitaCoreConfig.CAMERA_SOURCE_IMAGE, backCameraImage = "/here/back.png"
        ).withValidCameraSources { it.startsWith("/here") }
        assertEquals(VitaCoreConfig.CAMERA_SOURCE_REAL, config.frontCameraType)
        assertEquals("", config.frontCameraImage)
        assertEquals(VitaCoreConfig.CAMERA_SOURCE_IMAGE, config.backCameraType)
        assertEquals("/here/back.png", config.backCameraImage)
    }

    // ---- microphone settings ----

    @Test
    fun microphoneDefaultsToEnabledWithUnityGainAndTheSystemDevice() {
        val config = VitaCoreConfig()
        assertTrue(config.micEnabled)
        assertEquals(100, config.micGainPercent)
        assertEquals("", config.micInputDevice)
    }

    @Test
    fun microphoneGainIsClampedToTheSliderRange() {
        assertEquals(MIC_GAIN_MAX, VitaCoreConfig(micGainPercent = 5000).clampedToUiRanges().micGainPercent)
        assertEquals(MIC_GAIN_MIN, VitaCoreConfig(micGainPercent = -4).clampedToUiRanges().micGainPercent)
        assertEquals(150, VitaCoreConfig(micGainPercent = 150).clampedToUiRanges().micGainPercent)
    }

    @Test
    fun globalMicrophoneSettingsRoundTripThroughConfigYml() {
        val repository = VitaCoreConfigRepository(context)
        repository.save(VitaCoreConfig(micEnabled = false, micGainPercent = 220, micInputDevice = "22:USB Mic"))
        val loaded = VitaCoreConfigRepository(context).load()
        assertFalse(loaded.micEnabled)
        assertEquals(220, loaded.micGainPercent)
        assertEquals("22:USB Mic", loaded.micInputDevice)
    }

    @Test
    fun perGameMicrophoneOverrideWinsOverTheGlobalValue() {
        VitaCoreConfigRepository(context).save(VitaCoreConfig(micEnabled = true, micGainPercent = 100))
        val repository = VitaGameSettingsRepository(context)
        repository.saveProfile(titleId, VitaCoreConfig(micEnabled = false, micGainPercent = 40, micInputDevice = "7:Headset"), null)

        val effective = repository.loadEffective(titleId)
        assertFalse(effective.micEnabled)
        assertEquals(40, effective.micGainPercent)
        assertEquals("7:Headset", effective.micInputDevice)
        // Another game without a profile still follows the global setting.
        assertTrue(repository.loadEffective("PCSE99999").micEnabled)
    }

    @Test
    fun microphoneSettingsSurviveABackupRestore() {
        val json = org.json.JSONObject()
            .put("micEnabled", false).put("micGainPercent", 80).put("micInputDevice", "15:mic")
        // The key names are part of the backup format.
        assertEquals(false, json.getBoolean("micEnabled"))
        assertEquals(80, json.getInt("micGainPercent"))
        assertEquals("15:mic", json.getString("micInputDevice"))
    }

    // ---- microphone devices, level, permission ----

    @Test
    fun deviceKindsMatchTheAndroidInputTypes() {
        assertEquals(MicDeviceKind.BUILT_IN, micDeviceKind(15))
        assertEquals(MicDeviceKind.USB, micDeviceKind(11))
        assertEquals(MicDeviceKind.USB, micDeviceKind(22))
        assertEquals(MicDeviceKind.BLUETOOTH, micDeviceKind(7))
        assertEquals(MicDeviceKind.BLUETOOTH, micDeviceKind(26))
        assertEquals(MicDeviceKind.WIRED, micDeviceKind(3))
        assertEquals(MicDeviceKind.OTHER, micDeviceKind(18)) // telephony
        assertFalse(isSelectableMicType(18))
        assertTrue(isSelectableMicType(15))
    }

    @Test
    fun savedDeviceResolvesToItsCurrentIdOrTheSystemDefault() {
        val devices = listOf(MicInputDevice(41, 15, "Built-in"), MicInputDevice(77, 11, "USB Mic"))
        assertEquals(77, resolveMicDeviceId("11:USB Mic", devices))
        assertEquals(0, resolveMicDeviceId("11:Unplugged", devices))
        assertEquals(0, resolveMicDeviceId("", devices))
        assertEquals("11:USB Mic", devices[1].key)
    }

    @Test
    fun levelMeterMapsSilenceAndFullScaleToTheEnds() {
        assertEquals(0f, micLevel(ShortArray(100), 100), 0f)
        val loud = ShortArray(100) { if (it % 2 == 0) 32767 else -32768 }
        assertEquals(1f, micLevel(loud, 100), 0.01f)
        val quiet = ShortArray(100) { if (it % 2 == 0) 300 else -300 }
        val level = micLevel(quiet, 100)
        assertTrue(level > 0f && level < 1f)
        assertTrue(micLevel(quiet, 100, 300) > level)
        assertEquals(0f, micLevel(quiet, 100, 0), 0f)
        assertEquals(0f, micLevel(quiet, 0), 0f)
    }

    @Test
    fun permissionStatusFollowsGrantHistoryAndRationale() {
        assertEquals(PermissionStatus.GRANTED, permissionStatus(true, true, false))
        assertEquals(PermissionStatus.CAN_REQUEST, permissionStatus(false, false, false))
        assertEquals(PermissionStatus.CAN_REQUEST, permissionStatus(false, true, true))
        assertEquals(PermissionStatus.BLOCKED, permissionStatus(false, true, false))
    }

    @Test
    fun permissionHistoryIsRemembered() {
        val history = PermissionAskHistory(context)
        assertFalse(history.wasAsked("android.permission.RECORD_AUDIO"))
        history.markAsked("android.permission.RECORD_AUDIO")
        assertTrue(PermissionAskHistory(context).wasAsked("android.permission.RECORD_AUDIO"))
    }
}
