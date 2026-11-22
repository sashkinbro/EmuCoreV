package com.sbro.emucorev.core

/**
 * Title ids reach file paths (config_<id>.xml, ux0/app/<id>) and the native launch arguments, and
 * the Emulator activity is exported, so every id from an Intent or a repository call is checked
 * here first. Retail ids are four letters and five digits, but homebrew ids and the native-shell
 * pseudo titles (__VITA_NATIVE_LIBRARY__) use other shapes, so the accepted set is a safe
 * charset without separators or dots rather than the retail pattern.
 */
object TitleIds {
    private val SAFE = Regex("[A-Za-z0-9_]{1,32}")

    fun isValid(titleId: String?): Boolean = titleId != null && SAFE.matches(titleId)

    /** The trimmed id when valid, otherwise null. */
    fun sanitizeOrNull(titleId: String?): String? = titleId?.trim()?.takeIf(::isValid)
}
