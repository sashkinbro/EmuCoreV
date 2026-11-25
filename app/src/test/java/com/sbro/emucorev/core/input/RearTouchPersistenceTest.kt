package com.sbro.emucorev.core.input

import android.content.Context
import com.sbro.emucorev.core.SettingsBackupRepository
import com.sbro.emucorev.core.VitaCoreConfigRepository
import com.sbro.emucorev.data.AppPreferences
import com.sbro.emucorev.data.CustomizationPreferences
import org.json.JSONObject
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config

@RunWith(RobolectricTestRunner::class)
@Config(manifest = Config.NONE, sdk = [29])
class RearTouchPersistenceTest {
    private val context: Context get() = RuntimeEnvironment.getApplication()

    @After fun clear() {
        context.getSharedPreferences("rear_touch_profiles", Context.MODE_PRIVATE).edit().clear().commit()
    }

    @Test fun triggerModeAndZoneSurviveEncodeDecode() {
        val profile = RearTouchProfile(
            listOf(RearTouchPoint(0, trigger = RearTriggerMode.TOGGLE, positionMode = RearPositionMode.ZONE),
                RearTouchPoint(1, trigger = RearTriggerMode.TAP)),
            RearZone(.1f, .2f, .6f, .9f))
        val decoded = RearTouchProfileRepository.decode(RearTouchProfileRepository.encode(profile))!!
        assertEquals(RearTriggerMode.TOGGLE, decoded.points[0].trigger)
        assertEquals(RearPositionMode.ZONE, decoded.points[0].positionMode)
        assertEquals(RearTriggerMode.TAP, decoded.points[1].trigger)
        assertEquals(RearTriggerMode.HOLD, decoded.points[2].trigger)
        assertEquals(RearZone(.1f, .2f, .6f, .9f), decoded.zone)
    }

    @Test fun profilesWrittenBeforeTheNewFieldsStillLoadAsHoldAndFullZone() {
        val old = JSONObject(RearTouchProfileRepository.encode(RearTouchProfile()))
        old.remove("zone")
        for (i in 0 until old.getJSONArray("points").length()) old.getJSONArray("points").getJSONObject(i).remove("trigger")
        val decoded = RearTouchProfileRepository.decode(old.toString())!!
        assertEquals(RearZone.FULL, decoded.zone)
        assertTrue(decoded.points.all { it.trigger == RearTriggerMode.HOLD })
    }

    @Test fun touchModeIsRememberedPerTitleAndClearedWhenZero() {
        val repository = RearTouchProfileRepository(context)
        assertEquals(0, repository.touchMode("PCSE00001"))
        repository.setTouchMode("PCSE00001", 1)
        repository.setTouchMode("PCSE00002", 2)
        assertEquals(1, RearTouchProfileRepository(context).touchMode("PCSE00001"))
        assertEquals(2, repository.touchMode("PCSE00002"))
        repository.setTouchMode("PCSE00001", 0)
        assertEquals(0, repository.touchMode("PCSE00001"))
        repository.setTouchMode("not-an-id", 2) // ignored
        assertEquals(0, repository.touchMode("not-an-id"))
    }

    @Test fun rearProfilesAndTouchModeRoundTripThroughTheBackup() {
        val repository = RearTouchProfileRepository(context)
        val game = RearTouchProfile(listOf(RearTouchPoint(0, trigger = RearTriggerMode.TAP)))
        repository.saveGlobal(RearTouchProfile(zone = RearZone.FULL))
        repository.saveGame("PCSE00001", game)
        repository.setTouchMode("PCSE00001", 2)
        val exported = repository.exportAll()
        assertEquals(3, exported.length())

        context.getSharedPreferences("rear_touch_profiles", Context.MODE_PRIVATE).edit().clear().commit()
        assertNull(repository.override("PCSE00001"))
        repository.restoreAll(exported)
        assertEquals(RearTriggerMode.TAP, repository.override("PCSE00001")!!.points[0].trigger)
        assertEquals(2, repository.touchMode("PCSE00001"))
        // garbage and unknown keys are ignored
        repository.restoreAll(JSONObject().put("global", "{}").put("evil", "x").put("touch_mode_PCSE00009", 7))
        assertEquals(0, repository.touchMode("PCSE00009"))
    }

    @Test fun settingsBackupContainsTheRearProfiles() {
        RearTouchProfileRepository(context).saveGlobal(RearTouchProfile(zone = RearZone(0f, 0f, .5f, 1f)))
        val customization = CustomizationPreferences(context)
        try {
            val backup = SettingsBackupRepository(context, AppPreferences(context), VitaCoreConfigRepository(context), customization)
            val exported = backup.exportJson()
            assertTrue(exported.getJSONObject("rearTouch").has("global"))
            context.getSharedPreferences("rear_touch_profiles", Context.MODE_PRIVATE).edit().clear().commit()
            backup.restoreJson(exported, applyCore = false, applyCustomization = false)
            assertEquals(RearZone(0f, 0f, .5f, 1f), RearTouchProfileRepository(context).global().zone)
        } finally {
            customization.close()
        }
    }
}
