package com.sbro.emucorev.ui.cheats

import androidx.annotation.StringRes
import com.sbro.emucorev.R
import com.sbro.emucorev.core.VitaCheatEntry
import java.util.Locale

/** UI grouping for installed cheats, shared by the manager and the in-game menu. */
enum class CheatCategory(@StringRes val titleRes: Int) {
    PLAYER(R.string.cheat_manager_category_player),
    ITEMS(R.string.cheat_manager_category_items),
    WORLD(R.string.cheat_manager_category_world),
    PROGRESS(R.string.cheat_manager_category_progress),
    VEHICLES(R.string.cheat_manager_category_vehicles),
    STATS(R.string.cheat_manager_category_stats),
    HOTKEYS(R.string.cheat_manager_category_hotkeys),
    OTHER(R.string.cheat_manager_category_other)
}

fun VitaCheatEntry.category(): CheatCategory {
    val value = name.lowercase(Locale.US)
    return when {
        value.containsAny(" press ", "press ", "hold ", "button", "{l1}", "{l2}", "{r1}", "{r2}", "{select}") ->
            CheatCategory.HOTKEYS
        value.containsAny("health", " hp", "hp ", "life", "money", "gold", "stamina", "energy", "mana", "mp ", "player", "character", "rage", "magic", "infinite", "inf ") ->
            CheatCategory.PLAYER
        value.containsAny("weapon", "ammo", "inventory", "item", "skill", "orb", "soul", "material", "equipment", "armor", "potion", "card", "gem") ->
            CheatCategory.ITEMS
        value.containsAny("time", "hour", "clock", "weather", "day", "night", "season") ->
            CheatCategory.WORLD
        value.containsAny("mission", "chapter", "unlock", "complete", "progress", "troph", "level", "exp", "rank", "collectible") ->
            CheatCategory.PROGRESS
        value.containsAny("vehicle", "bike", "car", "kart", "race", "drift", "nitro", "speed") ->
            CheatCategory.VEHICLES
        value.startsWith("max ") || value.startsWith("no ") ||
            value.containsAny("stat", "record", "distance", "earned", "spent", "kills", "hits", "score") ->
            CheatCategory.STATS
        else -> CheatCategory.OTHER
    }
}

/** Cheats ordered by [CheatCategory]; categories without cheats are omitted. */
fun groupCheatEntries(entries: List<VitaCheatEntry>): List<Pair<CheatCategory, List<VitaCheatEntry>>> =
    CheatCategory.entries.mapNotNull { category ->
        entries.filter { entry -> entry.category() == category }
            .takeIf(List<VitaCheatEntry>::isNotEmpty)
            ?.let { category to it }
    }

private fun String.containsAny(vararg needles: String): Boolean = needles.any(::contains)
