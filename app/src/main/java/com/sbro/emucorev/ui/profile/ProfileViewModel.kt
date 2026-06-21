package com.sbro.emucorev.ui.profile

import android.app.Application
import android.content.Context
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.google.firebase.FirebaseNetworkException
import com.google.firebase.auth.FirebaseAuth
import com.google.firebase.auth.FirebaseAuthException
import com.google.firebase.auth.UserProfileChangeRequest
import com.sbro.emucorev.R
import com.sbro.emucorev.data.FirebaseProfileBackupRepository
import com.sbro.emucorev.data.ProfileCatalogGame
import com.sbro.emucorev.data.ProfileGameListRepository
import com.sbro.emucorev.data.ProfileGameStatus
import com.sbro.emucorev.data.await
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch

enum class ProfileLayoutMode {
    GRID,
    LIST
}

data class ProfileAccountState(
    val isSignedIn: Boolean = false,
    val displayName: String? = null,
    val email: String? = null,
    val photoUrl: String? = null,
    val localAvatarUrl: String? = null
)

data class ProfileUiState(
    val isLoading: Boolean = true,
    val isCloudBusy: Boolean = false,
    val layoutMode: ProfileLayoutMode = ProfileLayoutMode.GRID,
    val gamesByStatus: Map<ProfileGameStatus, List<ProfileCatalogGame>> = emptyMap(),
    val favoriteGames: List<ProfileCatalogGame> = emptyList(),
    val account: ProfileAccountState = ProfileAccountState(),
    val cloudMessage: String? = null
) {
    val totalCount: Int
        get() = (gamesByStatus.values.flatten() + favoriteGames).distinctBy { it.catalog.igdbId }.size

    val visibleStatuses: List<ProfileGameStatus>
        get() = ProfileGameStatus.entries.filter { gamesByStatus[it].orEmpty().isNotEmpty() }
}

class ProfileViewModel(application: Application) : AndroidViewModel(application) {
    private val profilePreferences = application.getSharedPreferences("profile_preferences", Context.MODE_PRIVATE)
    private val repository = ProfileGameListRepository(application)
    private val cloudRepository = FirebaseProfileBackupRepository()
    private val auth = FirebaseAuth.getInstance()
    private val authListener = FirebaseAuth.AuthStateListener { firebaseAuth ->
        _uiState.value = _uiState.value.copy(
            account = firebaseAuth.currentUser.toAccountState()
        )
    }

    private val _uiState = MutableStateFlow(ProfileUiState())
    val uiState: StateFlow<ProfileUiState> = _uiState.asStateFlow()

    init {
        auth.addAuthStateListener(authListener)
        refresh()
    }

    fun refresh() {
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(isLoading = true)
            val games = loadProfileGames()
            _uiState.value = _uiState.value.copy(
                isLoading = false,
                gamesByStatus = games.byStatus(),
                favoriteGames = games.favorites()
            )
        }
    }

    fun setLayoutMode(mode: ProfileLayoutMode) {
        _uiState.value = _uiState.value.copy(layoutMode = mode)
    }

    fun removeGame(igdbId: Long) {
        viewModelScope.launch(Dispatchers.IO) {
            repository.remove(igdbId)
            refreshProfileGames()
        }
    }

    fun setGameStatus(igdbId: Long, status: ProfileGameStatus) {
        viewModelScope.launch(Dispatchers.IO) {
            repository.setStatus(igdbId, status)
            refreshProfileGames()
        }
    }

    fun clearGameStatus(igdbId: Long) {
        viewModelScope.launch(Dispatchers.IO) {
            repository.clearStatus(igdbId)
            refreshProfileGames()
        }
    }

    fun setFavorite(igdbId: Long, favorite: Boolean) {
        viewModelScope.launch(Dispatchers.IO) {
            repository.setFavorite(igdbId, favorite)
            refreshProfileGames()
        }
    }

    fun showCloudMessage(message: String) {
        _uiState.value = _uiState.value.copy(cloudMessage = message)
    }

    fun signInWithEmail(email: String, password: String, displayName: String, createAccount: Boolean) {
        val cleanEmail = email.trim()
        val cleanDisplayName = displayName.trim()
        if (cleanEmail.isBlank() || password.length < 6) {
            _uiState.value = _uiState.value.copy(cloudMessage = "Enter an email and a password with at least 6 characters.")
            return
        }
        if (createAccount && cleanDisplayName.isBlank()) {
            _uiState.value = _uiState.value.copy(cloudMessage = "Enter a user name for the account.")
            return
        }
        viewModelScope.launch(Dispatchers.IO) {
            runCloudAction(errorMessage = { error -> formatEmailAuthError(error, createAccount) }) {
                if (createAccount) {
                    auth.createUserWithEmailAndPassword(cleanEmail, password).await()
                    auth.currentUser?.updateProfile(
                        UserProfileChangeRequest.Builder()
                            .setDisplayName(cleanDisplayName)
                            .build()
                    )?.await()
                    _uiState.value = _uiState.value.copy(account = auth.currentUser.toAccountState())
                    ""
                } else {
                    auth.signInWithEmailAndPassword(cleanEmail, password).await()
                    ""
                }
            }
        }
    }

    fun sendPasswordResetEmail(email: String) {
        val cleanEmail = email.trim()
        if (cleanEmail.isBlank()) {
            _uiState.value = _uiState.value.copy(
                cloudMessage = getApplication<Application>().getString(R.string.profile_password_reset_email_required)
            )
            return
        }
        viewModelScope.launch(Dispatchers.IO) {
            runCloudAction(errorMessage = ::formatPasswordResetError) {
                auth.sendPasswordResetEmail(cleanEmail).await()
                getApplication<Application>().getString(R.string.profile_password_reset_sent)
            }
        }
    }

    fun setLocalAvatar(uri: String) {
        profilePreferences.edit().putString(KEY_PROFILE_AVATAR_URI, uri).apply()
        _uiState.value = _uiState.value.copy(account = auth.currentUser.toAccountState())
    }

    fun signOut() {
        auth.signOut()
        _uiState.value = _uiState.value.copy(cloudMessage = null)
    }

    fun backupProfile() {
        viewModelScope.launch(Dispatchers.IO) {
            runCloudAction {
                cloudRepository.backup(repository.loadEntries())
                ""
            }
        }
    }

    fun restoreProfile() {
        viewModelScope.launch(Dispatchers.IO) {
            runCloudAction {
                val entries = cloudRepository.restore()
                repository.replaceAll(entries)
                refreshProfileGames()
                ""
            }
        }
    }

    fun clearCloudMessage() {
        _uiState.value = _uiState.value.copy(cloudMessage = null)
    }

    private suspend fun runCloudAction(
        errorMessage: (Throwable) -> String = ::formatCloudError,
        action: suspend () -> String
    ) {
        _uiState.value = _uiState.value.copy(isCloudBusy = true, cloudMessage = null)
        val message = runCatching { action() }
            .fold(
                onSuccess = { successMessage -> successMessage.takeIf { it.isNotBlank() } },
                onFailure = { error -> errorMessage(error) }
            )
        _uiState.value = _uiState.value.copy(isCloudBusy = false, cloudMessage = message)
    }

    private fun formatEmailAuthError(error: Throwable, createAccount: Boolean): String {
        val authError = error as? FirebaseAuthException
        return when (authError?.errorCode) {
            "ERROR_OPERATION_NOT_ALLOWED" -> "Email sign-in is not enabled for this app."
            "ERROR_EMAIL_ALREADY_IN_USE" -> "This email already has an account. Switch to sign in."
            "ERROR_WEAK_PASSWORD" -> "Use a stronger password with at least 6 characters."
            "ERROR_INVALID_EMAIL" -> "Enter a valid email address."
            "ERROR_USER_NOT_FOUND" -> "No account exists for this email. Switch to create account."
            "ERROR_WRONG_PASSWORD" -> "The password is incorrect."
            "ERROR_INVALID_CREDENTIAL" -> if (createAccount) {
                "Email account creation is not enabled for this app."
            } else {
                "Email or password is incorrect, or this account does not exist."
            }
            else -> formatCloudError(error)
        }
    }

    private fun formatPasswordResetError(error: Throwable): String {
        val authError = error as? FirebaseAuthException
        return when (authError?.errorCode) {
            "ERROR_INVALID_EMAIL" -> "Enter a valid email address."
            "ERROR_USER_NOT_FOUND" -> "No account exists for this email."
            "ERROR_OPERATION_NOT_ALLOWED" -> "Email password reset is not enabled for this app."
            else -> formatCloudError(error)
        }
    }

    private fun formatCloudError(error: Throwable): String {
        return when (error) {
            is FirebaseNetworkException -> "Network error. Check the connection and try again."
            is FirebaseAuthException -> error.localizedMessage ?: "Cloud sign-in failed: ${error.errorCode}."
            else -> error.localizedMessage ?: "Cloud action failed."
        }
    }

    private fun loadProfileGames(): List<ProfileCatalogGame> {
        return repository.loadCatalogGames()
    }

    private fun refreshProfileGames() {
        val games = loadProfileGames()
        _uiState.value = _uiState.value.copy(
            gamesByStatus = games.byStatus(),
            favoriteGames = games.favorites()
        )
    }

    private fun List<ProfileCatalogGame>.byStatus(): Map<ProfileGameStatus, List<ProfileCatalogGame>> {
        return mapNotNull { game -> game.profile.status?.let { it to game } }
            .groupBy({ it.first }, { it.second })
    }

    private fun List<ProfileCatalogGame>.favorites(): List<ProfileCatalogGame> {
        return filter { it.profile.isFavorite }
    }

    private fun com.google.firebase.auth.FirebaseUser?.toAccountState(): ProfileAccountState {
        val localAvatarUrl = profilePreferences.getString(KEY_PROFILE_AVATAR_URI, null)
        return ProfileAccountState(
            isSignedIn = this != null,
            displayName = this?.displayName,
            email = this?.email,
            photoUrl = localAvatarUrl ?: this?.photoUrl?.toString(),
            localAvatarUrl = localAvatarUrl
        )
    }

    override fun onCleared() {
        auth.removeAuthStateListener(authListener)
        super.onCleared()
    }
}

private const val KEY_PROFILE_AVATAR_URI = "profile_avatar_uri"
