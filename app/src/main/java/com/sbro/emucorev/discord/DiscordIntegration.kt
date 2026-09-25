package com.sbro.emucorev.discord

import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.content.ServiceConnection
import android.os.Bundle
import android.os.Handler
import android.os.IBinder
import android.os.Looper
import android.os.Message
import android.os.Messenger
import android.util.Log
import com.sbro.emucorev.BuildConfig
import com.sbro.emucorev.R
import com.sbro.emucorev.core.ProPurchaseManager
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.delay
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import kotlinx.coroutines.withContext

enum class DiscordConnectionStatus {
    Disabled,
    Unavailable,
    Disconnected,
    Authorizing,
    Connecting,
    Connected,
    Failed
}

data class DiscordFriend(
    val displayName: String,
    val activity: String,
    val avatarUrl: String
)

data class DiscordIntegrationState(
    val sdkAvailable: Boolean = BuildConfig.DISCORD_SDK_AVAILABLE,
    val enabled: Boolean = false,
    val status: DiscordConnectionStatus = if (BuildConfig.DISCORD_SDK_AVAILABLE) {
        DiscordConnectionStatus.Disabled
    } else {
        DiscordConnectionStatus.Unavailable
    },
    val accountName: String = "",
    val avatarUrl: String = "",
    val friends: List<DiscordFriend> = emptyList(),
    val shareGameTitle: Boolean = true,
    val shareGameSerial: Boolean = false,
    val error: String? = null
)

data class DiscordPresencePayload(
    val details: String,
    val state: String,
    val coverUrl: String
)

internal fun buildDiscordPresencePayload(
    gameTitle: String,
    gameSerial: String,
    paused: Boolean,
    shareGameTitle: Boolean,
    shareGameSerial: Boolean,
    libraryText: String,
    privateGameText: String,
    pausedText: String,
    titleText: (String) -> String,
    coverUrl: String,
    idleImageUrl: String
): DiscordPresencePayload {
    if (gameTitle.isBlank()) return DiscordPresencePayload(libraryText, "", idleImageUrl)
    val details = if (shareGameTitle) titleText(gameTitle) else privateGameText
    val state = when {
        paused -> pausedText
        shareGameSerial && gameSerial.isNotBlank() -> gameSerial
        else -> ""
    }
    return DiscordPresencePayload(
        details = details,
        state = state,
        coverUrl = coverUrl.takeIf { shareGameTitle && it.isNotBlank() } ?: idleImageUrl
    )
}

private const val DISCORD_STANDARD_ICON_URL =
    "https://raw.githubusercontent.com/sashkinbro/EmuCoreV/main/app/src/main/res/drawable-nodpi/ic_drawer_app.png"
private const val DISCORD_PRO_ICON_URL =
    "https://raw.githubusercontent.com/sashkinbro/EmuCoreV/main/app/src/main/res/drawable-nodpi/ic_drawer_app_pro.png"

internal fun discordIdleImageUrl(isProUnlocked: Boolean): String =
    if (isProUnlocked) DISCORD_PRO_ICON_URL else DISCORD_STANDARD_ICON_URL

internal fun parseDiscordFriends(encoded: String): List<DiscordFriend> = encoded
    .split(DiscordIpc.RECORD_SEPARATOR)
    .mapNotNull { record ->
        val fields = record.split(DiscordIpc.FIELD_SEPARATOR, limit = 3)
        fields.getOrNull(0)?.takeIf(String::isNotBlank)?.let { name ->
            DiscordFriend(
                displayName = name,
                activity = fields.getOrNull(1).orEmpty(),
                avatarUrl = fields.getOrNull(2).orEmpty()
            )
        }
    }

/** Main-process owner of the opt-in Discord connection, settings, and Rich Presence. */
object DiscordIntegration {
    private const val PREFERENCES = "discord_integration"
    private const val PREF_ENABLED = "enabled"
    private const val PREF_SHARE_TITLE = "share_title"
    private const val PREF_SHARE_SERIAL = "share_serial"
    private const val POLL_INTERVAL_MS = 1_000L

    private lateinit var appContext: Context
    private lateinit var tokenStore: DiscordTokenStore
    private val scope = CoroutineScope(SupervisorJob() + Dispatchers.Main.immediate)
    private val mutableState = MutableStateFlow(DiscordIntegrationState())
    val state = mutableState.asStateFlow()

    private var initialized = false
    private var helper: Messenger? = null
    private var bindRequested = false
    private var pendingAuthorization = false
    private var pollJob: Job? = null
    private var gameTitle = ""
    private var gameSerial = ""
    private var gameCoverUrl = ""
    private var gamePaused = false
    private var proUnlocked = false

    private val incoming = Messenger(Handler(Looper.getMainLooper()) { message ->
        if (message.what == DiscordIpc.MSG_STATE) applySnapshot(message.data ?: Bundle.EMPTY)
        true
    })

    private val connection = object : ServiceConnection {
        override fun onServiceConnected(name: ComponentName?, binder: IBinder?) {
            helper = binder?.let(::Messenger)
            val current = mutableState.value
            if (!current.enabled) {
                stopHelper(unbind = true)
                return
            }
            val savedToken = tokenStore.load()
            Log.i(TAG, "Discord helper connected (saved token=${savedToken.isNotBlank()})")
            mutableState.value = current.copy(
                status = if (savedToken.isBlank()) {
                    DiscordConnectionStatus.Disconnected
                } else {
                    DiscordConnectionStatus.Connecting
                },
                error = null
            )
            send(
                DiscordIpc.MSG_START,
                Bundle().apply { putString(DiscordIpc.DATA_TOKEN, savedToken) }
            )
            pushPresence()
            if (pendingAuthorization) {
                pendingAuthorization = false
                send(DiscordIpc.MSG_AUTHORIZE)
                mutableState.value = mutableState.value.copy(status = DiscordConnectionStatus.Authorizing)
            }
            send(DiscordIpc.MSG_QUERY)
        }

        override fun onServiceDisconnected(name: ComponentName?) {
            helper = null
            if (mutableState.value.enabled) {
                mutableState.value = mutableState.value.copy(
                    status = if (tokenStore.load().isBlank()) {
                        DiscordConnectionStatus.Disconnected
                    } else {
                        DiscordConnectionStatus.Connecting
                    }
                )
            }
        }

        override fun onBindingDied(name: ComponentName?) {
            helper = null
            rebindHelper()
        }

        override fun onNullBinding(name: ComponentName?) {
            helper = null
            bindRequested = false
            mutableState.value = mutableState.value.copy(
                status = DiscordConnectionStatus.Unavailable,
                error = appContext.getString(R.string.settings_discord_sdk_unavailable)
            )
        }
    }

    fun initialize(context: Context) {
        if (initialized) return
        initialized = true
        appContext = context.applicationContext
        tokenStore = DiscordTokenStore(appContext)
        val preferences = appContext.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
        val enabled = BuildConfig.DISCORD_SDK_AVAILABLE && preferences.getBoolean(PREF_ENABLED, false)
        mutableState.value = DiscordIntegrationState(
            enabled = enabled,
            shareGameTitle = preferences.getBoolean(PREF_SHARE_TITLE, true),
            shareGameSerial = preferences.getBoolean(PREF_SHARE_SERIAL, false),
            status = when {
                !BuildConfig.DISCORD_SDK_AVAILABLE -> DiscordConnectionStatus.Unavailable
                enabled && tokenStore.load().isNotBlank() -> DiscordConnectionStatus.Connecting
                enabled -> DiscordConnectionStatus.Disconnected
                else -> DiscordConnectionStatus.Disabled
            }
        )
        if (enabled) startHelper()
        val proManager = ProPurchaseManager.getInstance(appContext)
        proUnlocked = proManager.state.value.isProUnlocked
        scope.launch {
            proManager.state.collect { proState ->
                val unlocked = proState.isProUnlocked
                if (proUnlocked != unlocked) {
                    proUnlocked = unlocked
                    pushPresence()
                }
            }
        }
    }

    fun setEnabled(enabled: Boolean) {
        if (!initialized || !BuildConfig.DISCORD_SDK_AVAILABLE) return
        appContext.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
            .edit().putBoolean(PREF_ENABLED, enabled).apply()
        mutableState.value = mutableState.value.copy(
            enabled = enabled,
            status = when {
                !enabled -> DiscordConnectionStatus.Disabled
                tokenStore.load().isNotBlank() -> DiscordConnectionStatus.Connecting
                else -> DiscordConnectionStatus.Disconnected
            },
            error = null
        )
        if (enabled) startHelper() else stopHelper(unbind = true)
    }

    fun authorize() {
        if (!initialized || !BuildConfig.DISCORD_SDK_AVAILABLE) return
        if (!mutableState.value.enabled) setEnabled(true)
        pendingAuthorization = true
        mutableState.value = mutableState.value.copy(
            status = DiscordConnectionStatus.Authorizing,
            error = null
        )
        if (helper == null) {
            bindHelper()
        } else {
            pendingAuthorization = false
            send(DiscordIpc.MSG_AUTHORIZE)
        }
    }

    fun signOut() {
        if (!initialized) return
        tokenStore.clear()
        pendingAuthorization = false
        stopHelper(unbind = true)
        mutableState.value = mutableState.value.copy(
            status = if (mutableState.value.enabled) {
                DiscordConnectionStatus.Disconnected
            } else {
                DiscordConnectionStatus.Disabled
            },
            accountName = "",
            avatarUrl = "",
            friends = emptyList(),
            error = null
        )
    }

    fun setShareGameTitle(enabled: Boolean) {
        if (!initialized) return
        appContext.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
            .edit().putBoolean(PREF_SHARE_TITLE, enabled).apply()
        mutableState.value = mutableState.value.copy(shareGameTitle = enabled)
        pushPresence()
    }

    fun setShareGameSerial(enabled: Boolean) {
        if (!initialized) return
        appContext.getSharedPreferences(PREFERENCES, Context.MODE_PRIVATE)
            .edit().putBoolean(PREF_SHARE_SERIAL, enabled).apply()
        mutableState.value = mutableState.value.copy(shareGameSerial = enabled)
        pushPresence()
    }

    fun setPlaying(title: String, serial: String?) {
        if (!initialized) return
        gameTitle = title.trim()
        gameSerial = serial.orEmpty().trim()
        gameCoverUrl = ""
        gamePaused = false
        scope.launch {
            gameCoverUrl = withContext(Dispatchers.IO) {
                runCatching {
                    val catalog = com.sbro.emucorev.data.VitaCatalogRepository(appContext)
                    val cover = catalog.findBySerial(serial.orEmpty())?.coverUrl
                        ?: catalog.findBestMatch(title)?.coverUrl
                    cover.orEmpty()
                }.getOrDefault("")
            }
            pushPresence()
        }
    }

    fun setPaused(paused: Boolean) {
        if (!initialized || gameTitle.isBlank() || gamePaused == paused) return
        gamePaused = paused
        pushPresence()
    }

    fun clearGame() {
        if (!initialized) return
        gameTitle = ""
        gameSerial = ""
        gameCoverUrl = ""
        gamePaused = false
        pushPresence()
    }

    fun previewPayload(): DiscordPresencePayload {
        check(initialized) { "DiscordIntegration has not been initialized" }
        return presencePayload()
    }

    private fun startHelper() {
        if (!BuildConfig.DISCORD_SDK_AVAILABLE || !mutableState.value.enabled) return
        bindHelper()
        if (pollJob?.isActive != true) {
            pollJob = scope.launch {
                while (mutableState.value.enabled) {
                    if (helper == null) bindHelper() else send(DiscordIpc.MSG_QUERY)
                    delay(POLL_INTERVAL_MS)
                }
            }
        }
    }

    private fun bindHelper() {
        if (bindRequested || !BuildConfig.DISCORD_SDK_AVAILABLE || !mutableState.value.enabled) return
        bindRequested = runCatching {
            appContext.bindService(
                Intent(appContext, DiscordService::class.java),
                connection,
                Context.BIND_AUTO_CREATE
            )
        }.getOrDefault(false)
        if (!bindRequested) {
            mutableState.value = mutableState.value.copy(
                status = DiscordConnectionStatus.Failed,
                error = appContext.getString(R.string.settings_discord_service_error)
            )
        }
    }

    private fun stopHelper(unbind: Boolean) {
        pollJob?.cancel()
        pollJob = null
        send(DiscordIpc.MSG_STOP)
        helper = null
        if (unbind && bindRequested) {
            runCatching { appContext.unbindService(connection) }
            bindRequested = false
        }
    }

    private fun send(what: Int, data: Bundle = Bundle.EMPTY) {
        val target = helper ?: return
        runCatching {
            target.send(Message.obtain(null, what).apply {
                this.data = data
                replyTo = incoming
            })
        }.onFailure {
            Log.w(TAG, "Discord IPC send($what) failed", it)
            helper = null
            rebindHelper()
        }
    }

    private fun rebindHelper() {
        if (bindRequested) runCatching { appContext.unbindService(connection) }
        bindRequested = false
        if (mutableState.value.enabled) bindHelper()
    }

    private fun pushPresence() {
        if (!initialized || !mutableState.value.enabled || helper == null) return
        val payload = presencePayload()
        send(
            DiscordIpc.MSG_SET_PRESENCE,
            Bundle().apply {
                putString(DiscordIpc.DATA_DETAILS, payload.details)
                putString(DiscordIpc.DATA_STATE, payload.state)
                putString(DiscordIpc.DATA_COVER, payload.coverUrl)
            }
        )
    }

    private fun presencePayload(): DiscordPresencePayload {
        val current = mutableState.value
        return buildDiscordPresencePayload(
            gameTitle = gameTitle,
            gameSerial = gameSerial,
            paused = gamePaused,
            shareGameTitle = current.shareGameTitle,
            shareGameSerial = current.shareGameSerial,
            libraryText = appContext.getString(R.string.discord_presence_library),
            privateGameText = appContext.getString(R.string.discord_presence_private_game),
            pausedText = appContext.getString(R.string.discord_presence_paused),
            titleText = { title -> appContext.getString(R.string.discord_presence_playing, title) },
            coverUrl = gameCoverUrl,
            idleImageUrl = discordIdleImageUrl(proUnlocked)
        )
    }

    private fun applySnapshot(snapshot: Bundle) {
        val available = snapshot.getBoolean(DiscordIpc.DATA_AVAILABLE, BuildConfig.DISCORD_SDK_AVAILABLE)
        val freshToken = snapshot.getString(DiscordIpc.DATA_FRESH_TOKEN).orEmpty()
        val error = snapshot.getString(DiscordIpc.DATA_ERROR)?.takeIf(String::isNotBlank)
        val self = snapshot.getString(DiscordIpc.DATA_SELF).orEmpty()
            .split(DiscordIpc.FIELD_SEPARATOR, limit = 2)
        val friends = if (statusForSnapshot(snapshot, available) == DiscordConnectionStatus.Connected) {
            parseDiscordFriends(snapshot.getString(DiscordIpc.DATA_FRIENDS).orEmpty())
        } else {
            emptyList()
        }
        if (freshToken.isNotBlank() && !tokenStore.save(freshToken)) {
            mutableState.value = mutableState.value.copy(
                status = DiscordConnectionStatus.Failed,
                error = appContext.getString(R.string.settings_discord_token_error)
            )
            return
        }
        val status = statusForSnapshot(snapshot, available)
        mutableState.value = mutableState.value.copy(
            sdkAvailable = available,
            status = status,
            accountName = self.getOrNull(0).orEmpty(),
            avatarUrl = self.getOrNull(1).orEmpty(),
            friends = friends,
            error = error
        )
    }

    private fun statusForSnapshot(snapshot: Bundle, available: Boolean): DiscordConnectionStatus {
        if (!available) return DiscordConnectionStatus.Unavailable
        return when (snapshot.getInt(DiscordIpc.DATA_STATUS, 1)) {
            0 -> DiscordConnectionStatus.Disabled
            1 -> DiscordConnectionStatus.Disconnected
            2 -> DiscordConnectionStatus.Authorizing
            3 -> DiscordConnectionStatus.Connecting
            4 -> DiscordConnectionStatus.Connected
            else -> DiscordConnectionStatus.Failed
        }
    }

    private const val TAG = "EmuCoreVDiscord"

}
