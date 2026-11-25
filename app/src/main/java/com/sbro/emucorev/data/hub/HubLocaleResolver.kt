package com.sbro.emucorev.data.hub

import java.util.Locale

object HubLocaleResolver {
    val supportedLocales: Set<String> = setOf(
        "ar", "cs", "de", "en", "es", "fa", "fr", "hi", "id",
        "it", "ja", "ko", "pl", "pt", "ru", "tr", "uk", "zh"
    )

    fun resolve(languageTag: String?, available: Collection<String> = supportedLocales): String {
        val normalizedAvailable = available.map(::normalize).toSet()
        val normalized = normalize(languageTag.orEmpty())
        if (normalized in normalizedAvailable) return normalized
        val base = normalized.substringBefore('-')
        if (base in normalizedAvailable) return base
        return "en"
    }

    fun current(): String = resolve(Locale.getDefault().toLanguageTag())

    internal fun normalize(value: String): String {
        val raw = value.trim().replace('_', '-').lowercase(Locale.ROOT)
        return when (raw.substringBefore('-')) {
            "in" -> "id"
            "iw" -> "he"
            else -> raw
        }
    }
}
