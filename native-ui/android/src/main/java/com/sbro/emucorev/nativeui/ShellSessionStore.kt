package com.sbro.emucorev.nativeui

import java.io.File

/**
 * Keeps the native shell session (unlocked, Home focus, open LiveAreas) on disk. The shell
 * runs in its own process, which Android readily kills while a heavy game runs; when the game
 * ends the recreated shell must come back where the user was instead of on the lock screen.
 *
 * File format: first line `<boot id> <wall clock ms>`, the rest is the opaque session text
 * produced by the native shell. A session from another boot (or older than [MAX_AGE_MS]) is
 * never restored: after a restart the lock screen is the right start.
 */
class ShellSessionStore(
    private val file: File,
    private val bootId: () -> Long,
    private val now: () -> Long = System::currentTimeMillis,
) {
    private var lastSaved: String? = null

    /** Stores [session]; blank means there is nothing to remember and removes the file. */
    fun save(session: String) {
        if (session.isBlank()) { clear(); return }
        if (session == lastSaved && file.isFile) return
        runCatching {
            val temp = File(file.parentFile, file.name + ".tmp")
            temp.writeText("${bootId()} ${now()}\n$session")
            if (!temp.renameTo(file)) {
                file.delete()
                check(temp.renameTo(file)) { "Cannot replace ${file.name}" }
            }
            lastSaved = session
        }
    }

    /** The stored session text, or null when absent, stale, from another boot or unreadable. */
    fun load(): String? = runCatching {
        if (!file.isFile) return@runCatching null
        val text = file.readText()
        val split = text.indexOf('\n')
        if (split < 0) return@runCatching null
        val header = text.substring(0, split).split(' ')
        if (header.size != 2) return@runCatching null
        val boot = header[0].toLongOrNull() ?: return@runCatching null
        val savedAt = header[1].toLongOrNull() ?: return@runCatching null
        val age = now() - savedAt
        if (boot != bootId() || age < 0 || age > MAX_AGE_MS) return@runCatching null
        text.substring(split + 1).takeIf { it.isNotBlank() }
    }.getOrNull()

    fun clear() {
        lastSaved = null
        runCatching { file.delete() }
        runCatching { File(file.parentFile, file.name + ".tmp").delete() }
    }

    companion object {
        const val MAX_AGE_MS = 24L * 60 * 60 * 1000
        const val FILE_NAME = "shell-session.txt"

        /** Open LiveArea title ids named by a stored session (their images must be reloaded). */
        fun openTitles(session: String): List<String> =
            session.lineSequence().filter { it.startsWith("tab ") }.map { it.substring(4).trim() }
                .filter { it.isNotEmpty() }.toList()
    }
}
