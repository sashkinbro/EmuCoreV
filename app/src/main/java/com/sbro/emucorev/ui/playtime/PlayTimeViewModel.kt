package com.sbro.emucorev.ui.playtime

import android.app.Application
import androidx.lifecycle.AndroidViewModel
import androidx.lifecycle.viewModelScope
import com.sbro.emucorev.core.PlayTimeRepository
import com.sbro.emucorev.core.PlayTimeSession
import com.sbro.emucorev.data.InstalledGameRepository
import com.sbro.emucorev.data.InstalledVitaGame
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.flow.MutableStateFlow
import kotlinx.coroutines.flow.StateFlow
import kotlinx.coroutines.flow.asStateFlow
import kotlinx.coroutines.launch
import java.util.Calendar
import kotlin.math.max

data class PlayTimeGameStats(
    val titleId: String,
    val title: String,
    val iconPath: String?,
    val totalMs: Long,
    val sessionCount: Int,
    val lastPlayedAt: Long?
)

data class PlayTimeDayStats(
    val dayStartMs: Long,
    val label: PlayTimeDayLabel,
    val totalMs: Long
)

sealed interface PlayTimeDayLabel {
    data object Today : PlayTimeDayLabel
    data object Yesterday : PlayTimeDayLabel
    data class Date(val text: String) : PlayTimeDayLabel
}

data class PlayTimeUiState(
    val games: List<InstalledVitaGame> = emptyList(),
    val gameStats: List<PlayTimeGameStats> = emptyList(),
    val sessions: List<PlayTimeSession> = emptyList(),
    val dayStats: List<PlayTimeDayStats> = emptyList(),
    val selectedTitleId: String? = null,
    val isLoading: Boolean = true
) {
    val selectedGame: PlayTimeGameStats?
        get() = selectedTitleId?.let { titleId -> gameStats.firstOrNull { it.titleId.equals(titleId, ignoreCase = true) } }

    val visibleSessions: List<PlayTimeSession>
        get() = selectedTitleId
            ?.let { titleId -> sessions.filter { it.titleId.equals(titleId, ignoreCase = true) } }
            ?: sessions

    val totalMs: Long
        get() = (selectedGame?.totalMs ?: gameStats.sumOf { it.totalMs }).coerceAtLeast(0L)

    val sessionCount: Int
        get() = selectedGame?.sessionCount ?: sessions.size

    val gamesPlayedCount: Int
        get() = gameStats.count { it.totalMs > 0L }
}

class PlayTimeViewModel(application: Application) : AndroidViewModel(application) {
    private val playTimeRepository = PlayTimeRepository(application)
    private val gameRepository = InstalledGameRepository()

    private val _uiState = MutableStateFlow(PlayTimeUiState())
    val uiState: StateFlow<PlayTimeUiState> = _uiState.asStateFlow()

    init {
        refresh()
    }

    fun refresh(focusTitleId: String? = _uiState.value.selectedTitleId) {
        val context = getApplication<Application>()
        viewModelScope.launch(Dispatchers.IO) {
            _uiState.value = _uiState.value.copy(isLoading = true)
            val games = gameRepository.loadInstalledGames(context)
            val sessions = playTimeRepository.loadSessions()
                .sortedByDescending { it.startedAt }
            val selected = focusTitleId
                ?.takeIf { id ->
                    sessions.any { it.titleId.equals(id, ignoreCase = true) } ||
                        games.any { it.titleId.equals(id, ignoreCase = true) }
                }
            _uiState.value = PlayTimeUiState(
                games = games,
                gameStats = buildGameStats(games, sessions),
                sessions = sessions,
                dayStats = buildDayStats(sessions),
                selectedTitleId = selected,
                isLoading = false
            )
        }
    }

    fun selectGame(titleId: String?) {
        _uiState.value = _uiState.value.copy(selectedTitleId = titleId)
    }

    private fun buildGameStats(
        games: List<InstalledVitaGame>,
        sessions: List<PlayTimeSession>
    ): List<PlayTimeGameStats> {
        val installedByTitleId = games.associateBy { it.titleId.lowercase() }
        val titleIds = (games.map { it.titleId } + sessions.map { it.titleId })
            .distinctBy { it.lowercase() }
        return titleIds.map { titleId ->
            val installed = installedByTitleId[titleId.lowercase()]
            val gameSessions = sessions.filter { it.titleId.equals(titleId, ignoreCase = true) }
            PlayTimeGameStats(
                titleId = installed?.titleId ?: titleId,
                title = installed?.title ?: gameSessions.firstOrNull()?.title ?: titleId,
                iconPath = installed?.iconPath,
                totalMs = gameSessions.sumOf { it.effectiveDurationMs() },
                sessionCount = gameSessions.size,
                lastPlayedAt = gameSessions.maxOfOrNull { it.endedAt ?: it.startedAt }
            )
        }.sortedWith(
            compareByDescending<PlayTimeGameStats> { it.totalMs }
                .thenBy { it.title.lowercase() }
        )
    }

    private fun buildDayStats(sessions: List<PlayTimeSession>): List<PlayTimeDayStats> {
        val now = System.currentTimeMillis()
        val today = startOfDay(now)
        val firstDay = today - 13L * DAY_MS
        val totals = mutableMapOf<Long, Long>()

        sessions.forEach { session ->
            val sessionEnd = session.endedAt ?: max(session.startedAt, now)
            var cursor = max(session.startedAt, firstDay)
            while (cursor < sessionEnd) {
                val dayStart = startOfDay(cursor)
                val dayEnd = dayStart + DAY_MS
                val sliceEnd = minOf(sessionEnd, dayEnd)
                if (dayStart >= firstDay) {
                    totals[dayStart] = (totals[dayStart] ?: 0L) + max(0L, sliceEnd - cursor)
                }
                cursor = sliceEnd
            }
        }

        return (0..13).map { offset ->
            val dayStart = firstDay + offset * DAY_MS
            PlayTimeDayStats(
                dayStartMs = dayStart,
                label = dayLabel(dayStart, now),
                totalMs = totals[dayStart] ?: 0L
            )
        }
    }

    private fun startOfDay(timeMs: Long): Long {
        return Calendar.getInstance().apply {
            timeInMillis = timeMs
            set(Calendar.HOUR_OF_DAY, 0)
            set(Calendar.MINUTE, 0)
            set(Calendar.SECOND, 0)
            set(Calendar.MILLISECOND, 0)
        }.timeInMillis
    }

    private fun dayLabel(dayStartMs: Long, now: Long): PlayTimeDayLabel {
        val calendar = Calendar.getInstance().apply { timeInMillis = dayStartMs }
        val today = startOfDay(now)
        return when (dayStartMs) {
            today -> PlayTimeDayLabel.Today
            today - DAY_MS -> PlayTimeDayLabel.Yesterday
            else -> PlayTimeDayLabel.Date("${calendar.get(Calendar.DAY_OF_MONTH)}/${calendar.get(Calendar.MONTH) + 1}")
        }
    }

    private companion object {
        const val DAY_MS = 24L * 60L * 60L * 1_000L
    }
}
