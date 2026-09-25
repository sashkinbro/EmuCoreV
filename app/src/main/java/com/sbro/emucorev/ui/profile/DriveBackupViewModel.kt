package com.sbro.emucorev.ui.profile

import android.app.Activity
import android.app.Application
import android.app.PendingIntent
import android.content.Intent
import android.util.Log
import androidx.credentials.exceptions.GetCredentialCancellationException
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import androidx.work.WorkManager
import com.google.android.gms.auth.api.identity.Identity
import com.sbro.emucorev.data.drive.*
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineStart
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.flow.update
import kotlinx.coroutines.flow.combine
import kotlinx.coroutines.flow.stateIn
import kotlinx.coroutines.flow.SharingStarted
import kotlinx.coroutines.launch
import kotlinx.coroutines.sync.withLock
import kotlinx.coroutines.withContext
import java.io.File

data class DriveBackupUiState(
    val settings: DriveBackupSettings = DriveBackupSettings(),
    val copies: List<DriveBackupEntry> = emptyList(),
    val loading: Boolean = false,
    val queued: Boolean = false,
    val operation: DriveOperation = DriveOperation(),
    val message: String = "",
    val authInProgress: Boolean = false,
    val authorization: PendingIntent? = null
) { val busy get() = loading || queued || authInProgress || operation.phase.isNotEmpty() }

// Both navigation entry points (and secondary app activities) observe the same catalog and status.
// Authorization intents remain local to the initiating ViewModel so only one screen launches them.
private object SharedDriveUiState {
    val value = MutableStateFlow(DriveBackupUiState())
    var refreshNeeded = false
}

class DriveBackupViewModel(application: Application) : AndroidViewModel(application) {
    private val store = DriveBackupState.get(application)
    private val mutable = SharedDriveUiState.value
    private val authorization = MutableStateFlow<PendingIntent?>(null)
    val state = combine(mutable, authorization) { current, intent -> current.copy(authorization = intent) }
        .stateIn(viewModelScope, SharingStarted.Eagerly, mutable.value.copy(settings = store.value))
    private var pendingEmail = ""

    init {
        mutable.update { it.copy(settings = store.value) }
        viewModelScope.launch { store.flow.collect { settings -> mutable.update { it.copy(settings = settings) } } }
        viewModelScope.launch { DriveBackupWork.operation.collect { value -> mutable.update { it.copy(operation = value) } } }
        viewModelScope.launch {
            WorkManager.getInstance(application).getWorkInfosByTagFlow(DriveBackupWork.TAG).collect { work ->
                val active = work.any { !it.state.isFinished }
                val wasActive = mutable.value.queued
                mutable.update { it.copy(queued = active) }
                if (wasActive && !active) refresh()
            }
        }
        refresh()
    }

    fun refresh() {
        if (!store.value.connected) return
        if (mutable.value.loading || mutable.value.authInProgress) {
            SharedDriveUiState.refreshNeeded = true
            return
        }
        SharedDriveUiState.refreshNeeded = false
        action {
            withContext(Dispatchers.IO) {
                DriveBackupWork.mutex.withLock {
                    val api = DriveBackupApi(getApplication(), store.value.email)
                    val profile = api.verifyAccount()
                    store.update { it.copy(displayName = profile.displayName, photoUrl = profile.photoUrl) }
                    val copies = api.list()
                    mutable.update { it.copy(copies = copies) }
                }
            }
        }
    }

    fun connect(activity: Activity) = action {
        mutable.update { it.copy(authInProgress = true) }
        pendingEmail = DriveAuthorization.chooseAccount(activity)
        val result = DriveAuthorization.authorize(activity, pendingEmail)
        if (result.hasResolution()) authorization.value = result.pendingIntent
        else finishConnect()
    }

    fun authorizationLaunched() { authorization.value = null }

    fun authorizationResult(resultCode: Int, intent: Intent?) {
        if (resultCode != Activity.RESULT_OK || pendingEmail.isBlank()) {
            pendingEmail = ""; mutable.update { it.copy(authInProgress = false) }; return
        }
        action(allowAuthorization = true) {
            val result = Identity.getAuthorizationClient(getApplication<Application>()).getAuthorizationResultFromIntent(intent)
            if (result.accessToken == null) throw DriveBackupException("auth")
            finishConnect()
        }
    }

    private suspend fun finishConnect() = withContext(Dispatchers.IO) {
        val email = pendingEmail
        if (email.isBlank()) throw DriveBackupException("auth")
        val profile = DriveBackupApi(getApplication(), email).verifyAccount()
        DriveBackupWork.mutex.withLock {
            if (!store.value.email.equals(email, true)) {
                val app = getApplication<Application>()
                DriveBackupWork.cancel(app)
                DriveBackupArchive.clearChild(app.noBackupFilesDir, File(app.noBackupFilesDir, "drive-backup"))
                mutable.update { it.copy(copies = emptyList()) }
                store.update { DriveBackupSettings(email = email, displayName = profile.displayName,
                    photoUrl = profile.photoUrl, deviceId = it.deviceId) }
            } else store.update { it.copy(displayName = profile.displayName, photoUrl = profile.photoUrl,
                needsAuthorization = false, lastError = "") }
            DriveBackupWork.schedule(getApplication())
        }
        pendingEmail = ""
        DriveBackupWork.mutex.withLock {
            val copies = DriveBackupApi(getApplication(), email).list()
            mutable.update { it.copy(copies = copies) }
        }
    }

    fun disconnect() = action {
        DriveBackupWork.cancel(getApplication())
        withContext(Dispatchers.IO) {
            DriveBackupWork.mutex.withLock {
                val app = getApplication<Application>()
                mutable.update { it.copy(copies = emptyList()) }
                store.update { DriveBackupSettings(deviceId = it.deviceId) }
                DriveBackupWork.schedule(app)
                DriveBackupArchive.clearChild(app.noBackupFilesDir, File(app.noBackupFilesDir, "drive-backup"))
            }
        }
        mutable.update { it.copy(copies = emptyList()) }
    }

    fun configure(transform: (DriveBackupSettings) -> DriveBackupSettings) {
        if (mutable.value.busy) return
        val email = store.value.email
        // Local preferences do not enter the catalog/network loading state. Serialize writes
        // with account changes and workers, and apply each transform to the latest settings.
        viewModelScope.launch(start = CoroutineStart.UNDISPATCHED) {
            try {
                DriveBackupWork.mutex.withLock {
                    withContext(Dispatchers.IO) {
                        if (store.value.email != email) return@withContext
                        val previous = store.value
                        store.update(transform)
                        val next = store.value
                        if (previous.intervalHours != next.intervalHours || previous.wifiOnly != next.wifiOnly ||
                            previous.chargingOnly != next.chargingOnly) DriveBackupWork.schedule(getApplication())
                    }
                }
            } catch (error: CancellationException) { throw error }
            catch (_: Exception) { mutable.update { it.copy(message = "failed") } }
        }
    }

    fun backup() {
        if (mutable.value.busy) return
        mutable.update { it.copy(queued = true) }
        DriveBackupWork.enqueue(getApplication())
    }
    fun cancel() { DriveBackupWork.cancel(getApplication()) }
    fun dismissMessage() {
        mutable.update { it.copy(message = "") }
        viewModelScope.launch(Dispatchers.IO) { store.update { it.copy(lastError = "") } }
    }
    fun restore(entry: DriveBackupEntry, categories: Set<String>) {
        if (categories.isNotEmpty() && !mutable.value.busy) {
            mutable.update { it.copy(queued = true) }
            DriveBackupWork.enqueue(getApplication(), restoreId = entry.id, categories = categories)
        }
    }

    fun delete(entry: DriveBackupEntry) = action {
        withContext(Dispatchers.IO) {
            DriveBackupWork.mutex.withLock { DriveBackupApi(getApplication(), store.value.email).trash(entry.id) }
        }
        mutable.update { it.copy(copies = it.copies.filterNot { copy -> copy.id == entry.id }) }
    }

    private fun action(allowAuthorization: Boolean = false, block: suspend () -> Unit) {
        if (mutable.value.loading || (mutable.value.authInProgress && !allowAuthorization)) return
        viewModelScope.launch {
            mutable.update { it.copy(loading = true, message = "") }
            try { block() }
            catch (_: GetCredentialCancellationException) { pendingEmail = "" }
            catch (error: CancellationException) { throw error }
            catch (error: Exception) {
                pendingEmail = ""
                val diagnostic = when (error) {
                    is com.google.android.gms.common.api.ApiException -> "Google status=${error.statusCode}"
                    is DriveBackupException -> "reason=${error.reason}"
                    is androidx.credentials.exceptions.GetCredentialException -> "credential type=${error.type}"
                    else -> error.javaClass.simpleName
                }
                Log.w("DriveBackup", "Account operation failed: $diagnostic")
                val reason = when (error) {
                    is DriveBackupException -> error.reason
                    is com.google.android.gms.common.api.ApiException -> when (error.statusCode) {
                        10 -> "configuration" // DEVELOPER_ERROR: package/certificate not recognized
                        7 -> "network"
                        16 -> "auth" // CANCELED
                        else -> "failed"
                    }
                    is androidx.credentials.exceptions.GetCredentialProviderConfigurationException -> "configuration"
                    is androidx.credentials.exceptions.NoCredentialException -> "auth"
                    is androidx.credentials.exceptions.GetCredentialException -> "auth"
                    else -> "failed"
                }
                mutable.update { it.copy(message = reason) }
                if (reason == "auth") withContext(Dispatchers.IO) {
                    store.update { it.copy(needsAuthorization = true) }
                }
            } finally {
                mutable.update { it.copy(loading = false, authInProgress = pendingEmail.isNotEmpty()) }
                if (SharedDriveUiState.refreshNeeded && !mutable.value.authInProgress) refresh()
            }
        }
    }

    override fun onCleared() {
        if (pendingEmail.isNotEmpty()) mutable.update { it.copy(authInProgress = false) }
        super.onCleared()
    }
}
