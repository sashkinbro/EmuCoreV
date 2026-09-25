package com.sbro.emucorev.data

import android.content.Context

data class PlayerTrophyStat(
    val communicationId: String,
    val titleId: String?,
    val gameTitle: String,
    val setName: String,
    val iconPath: String?,
    val trophyCount: Int,
    val unlockedCount: Int,
    val platinumCount: Int,
    val goldCount: Int,
    val silverCount: Int,
    val bronzeCount: Int,
    val totalPoints: Int,
    val earnedPoints: Int,
    val lastUnlockedAtEpochSeconds: Long?
) {
    val completed: Boolean
        get() = trophyCount > 0 && unlockedCount >= trophyCount

    val progressFraction: Float
        get() = if (trophyCount <= 0) 0f else (unlockedCount.toFloat() / trophyCount).coerceIn(0f, 1f)
}

data class PlayerTrophySummary(
    val totalTrophies: Int = 0,
    val unlockedTrophies: Int = 0,
    val totalPoints: Int = 0,
    val earnedPoints: Int = 0,
    val sets: List<PlayerTrophyStat> = emptyList()
) {
    val progressFraction: Float
        get() = if (totalTrophies <= 0) 0f else (unlockedTrophies.toFloat() / totalTrophies).coerceIn(0f, 1f)
}

class VitaTrophyProfileRepository(private val context: Context) {

    fun loadSummary(): PlayerTrophySummary {
        val sets = runCatching { TrophyRepository().list(context) }.getOrDefault(emptyList())
        return summarize(sets)
    }

    suspend fun syncPublicSummary() {
        val summary = loadSummary()
        runCatching {
            PlayerProfileRepository(context)
                .updateAchievementSummary(summary.unlockedTrophies, summary.earnedPoints)
        }
    }

    private fun summarize(sets: List<VitaTrophySet>): PlayerTrophySummary {
        val stats = sets.map { set -> set.toStat() }
            .filter { it.trophyCount > 0 }
            .sortedWith(
                compareByDescending<PlayerTrophyStat> { it.unlockedCount > 0 }
                    .thenByDescending { it.completed }
                    .thenByDescending { it.lastUnlockedAtEpochSeconds ?: 0L }
                    .thenByDescending { it.unlockedCount }
                    .thenBy { it.gameTitle.lowercase() }
            )
        val totalTrophies = stats.sumOf { it.trophyCount }
        val unlockedTrophies = stats.sumOf { it.unlockedCount }
        val totalPoints = stats.sumOf { it.totalPoints }
        val earnedPoints = stats.sumOf { it.earnedPoints }
        return PlayerTrophySummary(
            totalTrophies = totalTrophies,
            unlockedTrophies = unlockedTrophies,
            totalPoints = totalPoints,
            earnedPoints = earnedPoints,
            sets = stats
        )
    }

    private fun VitaTrophySet.toStat(): PlayerTrophyStat {
        var platinum = 0
        var gold = 0
        var silver = 0
        var bronze = 0
        var totalPoints = 0
        var earnedPoints = 0
        var lastUnlockedAt: Long? = null
        trophies.forEach { trophy ->
            val points = pointsFor(trophy.grade)
            totalPoints += points
            when (trophy.grade) {
                VitaTrophyGrade.Platinum -> platinum++
                VitaTrophyGrade.Gold -> gold++
                VitaTrophyGrade.Silver -> silver++
                VitaTrophyGrade.Bronze -> bronze++
                VitaTrophyGrade.Unknown -> Unit
            }
            if (trophy.unlocked) {
                earnedPoints += points
                val unlockedAt = trophy.unlockedAtEpochSeconds
                if (unlockedAt != null) {
                    val current = lastUnlockedAt
                    if (current == null || unlockedAt > current) lastUnlockedAt = unlockedAt
                }
            }
        }
        return PlayerTrophyStat(
            communicationId = communicationId,
            titleId = titleId,
            gameTitle = gameTitle,
            setName = setName,
            iconPath = gameIconPath,
            trophyCount = trophyCount,
            unlockedCount = unlockedCount,
            platinumCount = platinum,
            goldCount = gold,
            silverCount = silver,
            bronzeCount = bronze,
            totalPoints = totalPoints,
            earnedPoints = earnedPoints,
            lastUnlockedAtEpochSeconds = lastUnlockedAt
        )
    }

    companion object {
        const val POINTS_PLATINUM = 180
        const val POINTS_GOLD = 90
        const val POINTS_SILVER = 30
        const val POINTS_BRONZE = 15

        fun pointsFor(grade: VitaTrophyGrade): Int = when (grade) {
            VitaTrophyGrade.Platinum -> POINTS_PLATINUM
            VitaTrophyGrade.Gold -> POINTS_GOLD
            VitaTrophyGrade.Silver -> POINTS_SILVER
            VitaTrophyGrade.Bronze -> POINTS_BRONZE
            VitaTrophyGrade.Unknown -> 0
        }
    }
}
