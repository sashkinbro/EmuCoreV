package com.sbro.emucorev.data

import android.content.Context
import android.net.Uri
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith
import org.robolectric.RobolectricTestRunner
import org.robolectric.RuntimeEnvironment
import org.robolectric.annotation.Config
import java.util.concurrent.CountDownLatch
import java.util.concurrent.Executors
import java.util.concurrent.TimeUnit

@RunWith(RobolectricTestRunner::class)
@Config(sdk = [35])
class TrophySoundRepositoryTest {
    private lateinit var context: Context
    private lateinit var access: FakeTrophySoundUriAccess
    private lateinit var repository: TrophySoundRepository

    @Before
    fun setUp() {
        context = RuntimeEnvironment.getApplication()
        context.getSharedPreferences(TEST_PREFERENCES, Context.MODE_PRIVATE)
            .edit().clear().commit()
        access = FakeTrophySoundUriAccess()
        repository = TrophySoundRepository(context, access, TEST_PREFERENCES)
    }

    @After
    fun tearDown() {
        context.getSharedPreferences(TEST_PREFERENCES, Context.MODE_PRIVATE)
            .edit().clear().commit()
    }

    @Test
    fun defaultsToSystemSoundAndPersistsModeAndPickedCustomUri() {
        assertEquals(TrophySoundMode.SYSTEM, repository.current().mode)
        assertFalse(repository.selectMode(TrophySoundMode.CUSTOM))

        val uri = Uri.parse("content://audio/trophy.ogg")
        access.readable += uri
        access.names[uri] = "trophy.ogg"
        assertEquals(TrophySoundSelectionResult.ACCEPTED, repository.selectCustomSound(uri))

        val restored = TrophySoundRepository(context, access, TEST_PREFERENCES).current()
        assertEquals(TrophySoundMode.CUSTOM, restored.mode)
        assertEquals(uri, restored.customUri)
        assertEquals("trophy.ogg", restored.customName)
    }

    @Test
    fun switchingModesKeepsCustomChoiceAndOffNeverRoutesToAudio() {
        val uri = Uri.parse("content://audio/custom.wav")
        access.readable += uri
        assertEquals(TrophySoundSelectionResult.ACCEPTED, repository.selectCustomSound(uri))

        assertTrue(repository.selectMode(TrophySoundMode.OFF))
        assertEquals(TrophySoundPlaybackRoute.Silent, resolveTrophySoundPlayback(repository.current()))
        assertTrue(repository.selectMode(TrophySoundMode.CUSTOM))
        assertEquals(TrophySoundPlaybackRoute.Custom(uri), resolveTrophySoundPlayback(repository.current()))
        assertEquals(uri, repository.current().customUri)
    }

    @Test
    fun rejectedPickerUriLeavesPriorSelectionAndGrantUntouched() {
        val existing = Uri.parse("content://audio/old.ogg")
        access.readable += existing
        assertEquals(TrophySoundSelectionResult.ACCEPTED, repository.selectCustomSound(existing))
        assertTrue(repository.selectMode(TrophySoundMode.OFF))

        val rejected = Uri.parse("content://audio/private.ogg")
        access.rejectGrant = true
        assertEquals(TrophySoundSelectionResult.REJECTED, repository.selectCustomSound(rejected))

        assertEquals(TrophySoundMode.OFF, repository.current().mode)
        assertEquals(existing, repository.current().customUri)
        assertTrue(access.released.isEmpty())
    }

    @Test
    fun inaccessibleCustomChoiceFallsBackToSystemRouteAndCanBeRevalidated() {
        val uri = Uri.parse("content://audio/revoked.ogg")
        access.readable += uri
        assertEquals(TrophySoundSelectionResult.ACCEPTED, repository.selectCustomSound(uri))
        access.readable -= uri

        assertFalse(repository.refreshCustomAvailability())
        assertTrue(repository.current().customUnavailable)
        assertEquals(TrophySoundPlaybackRoute.SystemNotification, resolveTrophySoundPlayback(repository.current()))

        access.readable += uri
        assertTrue(repository.refreshCustomAvailability())
        assertFalse(repository.current().customUnavailable)
        assertEquals(TrophySoundPlaybackRoute.Custom(uri), resolveTrophySoundPlayback(repository.current()))
    }

    @Test
    fun restoredCustomUriIsMarkedUnavailableUntilThisDeviceCanReadIt() {
        val uri = Uri.parse("content://other-device/audio.ogg")
        repository.restoreBackup(TrophySoundMode.CUSTOM.name, uri.toString(), "audio.ogg")
        assertFalse(repository.refreshCustomAvailability())
        assertEquals(TrophySoundMode.CUSTOM, repository.current().mode)
        assertTrue(repository.current().customUnavailable)
        assertEquals(TrophySoundPlaybackRoute.SystemNotification, resolveTrophySoundPlayback(repository.current()))
    }

    @Test
    fun newerOffSelectionWinsOverBlockedPickerValidation() {
        val uri = Uri.parse("content://audio/slow-provider.ogg")
        access.readable += uri
        val enteredRead = CountDownLatch(1)
        val continueRead = CountDownLatch(1)
        access.blockedReadUri = uri
        access.readEntered = enteredRead
        access.continueRead = continueRead
        val executor = Executors.newSingleThreadExecutor()
        try {
            val selection = executor.submit<TrophySoundSelectionResult> { repository.selectCustomSound(uri) }
            assertTrue(enteredRead.await(2, TimeUnit.SECONDS))
            assertTrue(repository.selectMode(TrophySoundMode.OFF))
            continueRead.countDown()

            assertEquals(TrophySoundSelectionResult.SUPERSEDED, selection.get(2, TimeUnit.SECONDS))
            assertEquals(TrophySoundMode.OFF, repository.current().mode)
            assertNull(repository.current().customUri)
            assertTrue(uri in access.released)
        } finally {
            continueRead.countDown()
            executor.shutdownNow()
        }
    }

    @Test
    fun slowAvailabilityRefreshForOldUriCannotMarkNewChoiceUnavailable() {
        val oldUri = Uri.parse("content://audio/old.ogg")
        val newUri = Uri.parse("content://audio/new.ogg")
        access.readable += oldUri
        access.readable += newUri
        assertEquals(TrophySoundSelectionResult.ACCEPTED, repository.selectCustomSound(oldUri))

        val enteredRead = CountDownLatch(1)
        val continueRead = CountDownLatch(1)
        access.blockedReadUri = oldUri
        access.readEntered = enteredRead
        access.continueRead = continueRead
        val executor = Executors.newSingleThreadExecutor()
        try {
            val refresh = executor.submit<Boolean> { repository.refreshCustomAvailability() }
            assertTrue(enteredRead.await(2, TimeUnit.SECONDS))
            assertEquals(TrophySoundSelectionResult.ACCEPTED, repository.selectCustomSound(newUri))
            continueRead.countDown()

            assertFalse(refresh.get(2, TimeUnit.SECONDS))
            assertEquals(newUri, repository.current().customUri)
            assertFalse(repository.current().customUnavailable)
        } finally {
            continueRead.countDown()
            executor.shutdownNow()
        }
    }

    @Test
    fun backupRestoreFromAnotherRepositorySupersedesBlockedPickerResult() {
        val uri = Uri.parse("content://audio/slow-picker.ogg")
        access.readable += uri
        val enteredRead = CountDownLatch(1)
        val continueRead = CountDownLatch(1)
        access.blockedReadUri = uri
        access.readEntered = enteredRead
        access.continueRead = continueRead
        val backupRepository = TrophySoundRepository(
            context,
            FakeTrophySoundUriAccess(),
            TEST_PREFERENCES
        )
        val executor = Executors.newSingleThreadExecutor()
        try {
            val selection = executor.submit<TrophySoundSelectionResult> { repository.selectCustomSound(uri) }
            assertTrue(enteredRead.await(2, TimeUnit.SECONDS))
            assertTrue(backupRepository.restoreBackup(TrophySoundMode.OFF.name, null, null))
            continueRead.countDown()

            assertEquals(TrophySoundSelectionResult.SUPERSEDED, selection.get(2, TimeUnit.SECONDS))
            assertEquals(TrophySoundMode.OFF, repository.current().mode)
            assertNull(repository.current().customUri)
            assertTrue(uri in access.released)
        } finally {
            continueRead.countDown()
            executor.shutdownNow()
        }
    }

    private class FakeTrophySoundUriAccess : TrophySoundUriAccess {
        val readable = mutableSetOf<Uri>()
        val names = mutableMapOf<Uri, String>()
        val released = mutableListOf<Uri>()
        val persisted = mutableSetOf<Uri>()
        var rejectGrant = false
        var blockedReadUri: Uri? = null
        var readEntered: CountDownLatch? = null
        var continueRead: CountDownLatch? = null

        override fun hasPersistedPermission(uri: Uri): Boolean = uri in persisted
        override fun persistReadablePermission(uri: Uri): Boolean {
            if (rejectGrant || uri !in readable) return false
            persisted += uri
            return true
        }
        override fun isReadable(uri: Uri): Boolean {
            if (uri == blockedReadUri) {
                readEntered?.countDown()
                check(continueRead?.await(2, TimeUnit.SECONDS) == true) { "test did not release blocked URI read" }
            }
            return uri in readable
        }
        override fun displayName(uri: Uri): String? = names[uri]
        override fun releasePermission(uri: Uri) {
            released += uri
            persisted -= uri
        }
    }

    private companion object {
        const val TEST_PREFERENCES = "trophy_sound_test"
    }
}
