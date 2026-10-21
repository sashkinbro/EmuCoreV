package com.sbro.emucorev.core

import java.util.Locale

object GpuModelParser {
    private val EMULATOR_MARKERS = listOf(
        "swiftshader",
        "llvmpipe",
        "android emulator",
        "goldfish",
        "virtualbox",
        "vmware",
        "qemu"
    )

    private val ADRENO = Regex("""adreno[\s\-]*(?:\(tm\)\s*)?(\d{3})""", RegexOption.IGNORE_CASE)
    private val IMMORTALIS =
        Regex("""immortalis[\s\-]*g\s*(\d{3})(?:\s*((?:mc|mp)\s*\d{1,2}))?""", RegexOption.IGNORE_CASE)
    private val MALI_NEXT_GEN =
        Regex(
            """mali[\s\-]*g\s*([12])(?:[\s\-]+(ultra(?:[\s\-]+nx)?))?(?:[\s\-]*((?:mc|mp)\s*\d{1,2}))?""",
            RegexOption.IGNORE_CASE
        )
    private val MALI =
        Regex("""mali[\s\-]*(g|t)?\s*(\d{2,4})(?:\s*((?:mc|mp)\s*\d{1,2}))?""", RegexOption.IGNORE_CASE)
    private val XCLIPSE = Regex("""xclipse[\s\-]*(\d{3})""", RegexOption.IGNORE_CASE)
    private val MALEOON = Regex("""maleoon[\s\-]*(\d{3})""", RegexOption.IGNORE_CASE)
    private val POWERVR_CXT =
        Regex("""cxt(p)?[\s\-]*(\d{2,5}(?:[\s\-]+\d{2,5})*)""", RegexOption.IGNORE_CASE)
    private val POWERVR_DXT =
        Regex("""dxt[\s\-]*(\d{2,5}(?:[\s\-]+\d{2,5})*)""", RegexOption.IGNORE_CASE)
    private val POWERVR_GE = Regex("""\bge[\s\-]*(\d{4})""", RegexOption.IGNORE_CASE)
    private val IMG_VENDOR = Regex("""\bimg\b""", RegexOption.IGNORE_CASE)

    fun parse(renderer: String?): GpuModelInfo? {
        val raw = renderer?.trim().orEmpty()
        if (raw.isEmpty()) return null
        val normalized = raw
            .replace("(TM)", " ", ignoreCase = true)
            .replace("(R)", " ", ignoreCase = true)
        val lower = normalized.lowercase(Locale.US)
        if (EMULATOR_MARKERS.any(lower::contains)) return null
        return parseAdreno(lower)
            ?: parseMali(lower)
            ?: parseMaleoon(lower)
            ?: parseXclipse(lower)
            ?: parsePowerVr(lower)
    }

    private fun parseAdreno(text: String): GpuModelInfo? {
        val match = ADRENO.find(text) ?: return null
        return GpuModelInfo(GpuVendor.ADRENO, "Adreno ${match.groupValues[1]}")
    }

    private fun parseMali(text: String): GpuModelInfo? {
        IMMORTALIS.find(text)?.let { match ->
            val name = "Immortalis-G${match.groupValues[1]}" + coreSuffix(match.groupValues[2])
            return GpuModelInfo(GpuVendor.MALI, name)
        }
        MALI_NEXT_GEN.find(text)?.let { match ->
            val tier = match.groupValues[2]
                .replace(Regex("\\s+"), " ")
                .trim()
                .replace("ultra", "Ultra", ignoreCase = true)
                .replace("nx", "NX", ignoreCase = true)
            val name = buildString {
                append("Mali-G")
                append(match.groupValues[1])
                if (tier.isNotEmpty()) {
                    append(' ')
                    append(tier)
                }
                append(coreSuffix(match.groupValues[3]))
            }
            return GpuModelInfo(GpuVendor.MALI, name)
        }
        val match = MALI.find(text) ?: return null
        val prefix = when (match.groupValues[1]) {
            "g" -> "Mali-G"
            "t" -> "Mali-T"
            else -> "Mali-"
        }
        val name = prefix + match.groupValues[2] + coreSuffix(match.groupValues[3])
        return GpuModelInfo(GpuVendor.MALI, name)
    }

    private fun parseMaleoon(text: String): GpuModelInfo? {
        val match = MALEOON.find(text) ?: return null
        return GpuModelInfo(GpuVendor.UNKNOWN, "Maleoon ${match.groupValues[1]}")
    }

    private fun parseXclipse(text: String): GpuModelInfo? {
        val match = XCLIPSE.find(text) ?: return null
        return GpuModelInfo(GpuVendor.XCLIPSE, "Xclipse ${match.groupValues[1]}")
    }

    private fun parsePowerVr(text: String): GpuModelInfo? {
        if (!text.contains("powervr") && !text.contains("imgtec") &&
            !text.contains("imagination") && !IMG_VENDOR.containsMatchIn(text)
        ) {
            return null
        }
        POWERVR_CXT.find(text)?.let { match ->
            val generation = if (match.groupValues[1].isNotEmpty()) "CXTP" else "CXT"
            val id = match.groupValues[2].replace(Regex("\\s+"), "-")
            return GpuModelInfo(GpuVendor.POWERVR, "PowerVR $generation-$id")
        }
        POWERVR_DXT.find(text)?.let { match ->
            val id = match.groupValues[1].replace(Regex("\\s+"), "-")
            return GpuModelInfo(GpuVendor.POWERVR, "PowerVR DXT-$id")
        }
        POWERVR_GE.find(text)?.let { match ->
            return GpuModelInfo(GpuVendor.POWERVR, "PowerVR GE${match.groupValues[1]}")
        }
        return GpuModelInfo(GpuVendor.POWERVR, "PowerVR")
    }

    private fun coreSuffix(raw: String): String {
        val cleaned = raw.replace(Regex("\\s+"), "").uppercase(Locale.US)
        return if (cleaned.isEmpty()) "" else " $cleaned"
    }
}

internal fun parseGpuModelName(renderer: String?): String? =
    GpuModelParser.parse(renderer)?.displayName
