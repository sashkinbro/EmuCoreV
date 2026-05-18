package com.sbro.emucorev.data

import android.content.Context
import android.util.Xml
import com.sbro.emucorev.core.EmulatorStorage
import org.xmlpull.v1.XmlPullParser
import java.io.File
import java.io.RandomAccessFile
import java.io.StringReader
import java.nio.ByteBuffer
import java.nio.ByteOrder

enum class VitaTrophyGrade {
    Platinum,
    Gold,
    Silver,
    Bronze,
    Unknown
}

data class VitaTrophy(
    val id: Int,
    val groupId: Int,
    val grade: VitaTrophyGrade,
    val hidden: Boolean,
    val name: String,
    val detail: String,
    val unlocked: Boolean,
    val unlockedAtEpochSeconds: Long?,
    val iconPath: String?
)

data class VitaTrophyGroup(
    val id: Int,
    val name: String,
    val detail: String,
    val trophies: List<VitaTrophy>
)

data class VitaTrophySet(
    val communicationId: String,
    val titleId: String?,
    val gameTitle: String,
    val gameIconPath: String?,
    val setName: String,
    val setDetail: String,
    val groups: List<VitaTrophyGroup>
) {
    val trophies: List<VitaTrophy> = groups.flatMap { it.trophies }
    val trophyCount: Int = trophies.size
    val unlockedCount: Int = trophies.count { it.unlocked }
}

class TrophyRepository {
    fun list(context: Context): List<VitaTrophySet> {
        val installedGames = InstalledGameRepository().loadInstalledGames(context)
        val installedPackages = installedGames.flatMap { game ->
            game.trophyPackages().mapNotNull { packageDir ->
                val trp = File(packageDir, TROPHY_TRP_NAME).takeIf { it.isFile } ?: return@mapNotNull null
                val commId = packageDir.name.takeIf { it.isLikelyTrophyCommunicationId() }
                    ?: TrpArchive(trp).readTextEntry(TROPCONF_NAME)?.let(::parseCommunicationId)
                    ?: return@mapNotNull null
                InstalledTrophyPackage(commId, packageDir, game)
            }
        }
        val installedByCommId = installedPackages.associateBy { it.communicationId }

        val commIds = linkedSetOf<String>()
        installedByCommId.keys.forEach(commIds::add)
        trophyConfRoots(context).forEach { root ->
            root.listFiles().orEmpty()
                .filter(File::isDirectory)
                .mapTo(commIds) { it.name }
        }

        return commIds.mapNotNull { commId ->
            val installedPackage = installedByCommId[commId]
            loadSet(context, commId, installedPackage)
        }.sortedWith(
            compareByDescending<VitaTrophySet> { it.unlockedCount > 0 }
                .thenBy { it.gameTitle.lowercase() }
                .thenBy { it.communicationId.lowercase() }
        )
    }

    fun loadForTitle(context: Context, titleId: String): List<VitaTrophySet> {
        val normalized = titleId.lowercase()
        return list(context).filter { it.titleId?.lowercase() == normalized }
    }

    private fun loadSet(
        context: Context,
        commId: String,
        installedPackage: InstalledTrophyPackage?
    ): VitaTrophySet? {
        val game = installedPackage?.game
        val confDir = trophyConfRoots(context)
            .map { File(it, commId) }
            .firstOrNull { it.isDirectory }
        val trpFile = installedPackage
            ?.directory
            ?.let { File(it, TROPHY_TRP_NAME) }
            ?.takeIf { it.isFile }

        val trp = trpFile?.let { runCatching { TrpArchive(it) }.getOrNull() }
        val configXml = readText(confDir?.resolve("TROPCONF.SFM"))
            ?: trp?.readTextEntry("TROPCONF.SFM")
            ?: return null
        val detailXml = preferredDetailXml(confDir, trp) ?: configXml
        val progress = findProgressFile(context, commId)
            ?.let { runCatching { TrophyProgress.read(it) }.getOrNull() }
            ?: TrophyProgress.Empty

        val config = parseConfigXml(configXml)
        if (config.trophies.isEmpty()) return null
        val details = parseDetailXml(detailXml)
        val iconDir = confDir ?: extractIcons(context, commId, trp)

        val trophies = config.trophies.map { item ->
            val trophyDetail = details.trophies[item.id]
            val hiddenLocked = item.hidden && !progress.isUnlocked(item.id)
            VitaTrophy(
                id = item.id,
                groupId = item.groupId,
                grade = progress.grade(item.id) ?: item.grade,
                hidden = item.hidden,
                name = if (hiddenLocked) "" else trophyDetail?.name.orEmpty(),
                detail = if (hiddenLocked) "" else trophyDetail?.detail.orEmpty(),
                unlocked = progress.isUnlocked(item.id),
                unlockedAtEpochSeconds = progress.unlockedAt(item.id),
                iconPath = iconDir?.resolve("TROP${item.id.toString().padStart(3, '0')}.PNG")
                    ?.takeIf { it.isFile }
                    ?.absolutePath
            )
        }.sortedBy { it.id }

        val groups = trophies.groupBy { it.groupId }
            .map { (groupId, groupTrophies) ->
                val detail = details.groups[groupId]
                VitaTrophyGroup(
                    id = groupId,
                    name = detail?.name?.takeIf(String::isNotBlank)
                        ?: if (groupId == 0) details.setName else "Group $groupId",
                    detail = detail?.detail.orEmpty(),
                    trophies = groupTrophies
                )
            }
            .sortedBy { it.id }

        val fallbackName = game?.title ?: details.setName.takeIf(String::isNotBlank) ?: commId
        return VitaTrophySet(
            communicationId = commId,
            titleId = game?.titleId,
            gameTitle = game?.title ?: fallbackName,
            gameIconPath = game?.iconPath,
            setName = details.setName.takeIf(String::isNotBlank) ?: fallbackName,
            setDetail = details.setDetail,
            groups = groups
        )
    }

    private fun trophyConfRoots(context: Context): List<File> {
        val userRoot = File(EmulatorStorage.vitaRoot(context), "ux0/user")
        val roots = mutableListOf<File>()
        userRoot.listFiles().orEmpty()
            .filter(File::isDirectory)
            .mapTo(roots) { File(it, "trophy/conf") }
        roots += File(userRoot, "trophy/conf")
        return roots.filter { it.isDirectory }.distinctBy { it.absolutePath }
    }

    private fun trophyDataRoots(context: Context): List<File> {
        val userRoot = File(EmulatorStorage.vitaRoot(context), "ux0/user")
        val roots = mutableListOf<File>()
        userRoot.listFiles().orEmpty()
            .filter(File::isDirectory)
            .mapTo(roots) { File(it, "trophy/data") }
        roots += File(userRoot, "trophy/data")
        return roots.filter { it.isDirectory }.distinctBy { it.absolutePath }
    }

    private fun findProgressFile(context: Context, commId: String): File? {
        return trophyDataRoots(context)
            .map { File(File(it, commId), "TROPUSR.DAT") }
            .firstOrNull { it.isFile }
    }

    private fun preferredDetailXml(confDir: File?, trp: TrpArchive?): String? {
        val confFiles = confDir?.listFiles().orEmpty()
        val preferredFile = confFiles.firstOrNull { it.name.equals("TROP.SFM", ignoreCase = true) }
            ?: confFiles.firstOrNull { it.name.equals("TROP_00.SFM", ignoreCase = true) }
            ?: confFiles.firstOrNull { it.name.startsWith("TROP_", ignoreCase = true) && it.extension.equals("SFM", ignoreCase = true) }
        if (preferredFile != null) return readText(preferredFile)

        return trp?.readTextEntry("TROP.SFM")
            ?: trp?.readTextEntry("TROP_00.SFM")
            ?: trp?.entryNames()
                ?.firstOrNull { it.startsWith("TROP_", ignoreCase = true) && it.endsWith(".SFM", ignoreCase = true) }
                ?.let(trp::readTextEntry)
    }

    private fun extractIcons(context: Context, commId: String, trp: TrpArchive?): File? {
        trp ?: return null
        val outputDir = File(EmulatorStorage.cacheRoot(context), "trophies/$commId").apply { mkdirs() }
        trp.entryNames()
            .filter { it.startsWith("TROP", ignoreCase = true) && it.endsWith(".PNG", ignoreCase = true) }
            .forEach { name ->
                val target = File(outputDir, name.uppercase())
                if (!target.isFile) {
                    runCatching { target.writeBytes(trp.readEntry(name)) }
                }
            }
        return outputDir.takeIf { it.listFiles().orEmpty().any(File::isFile) }
    }

    private fun InstalledVitaGame.trophyPackages(): List<File> {
        val trophyRoot = File(installPath, "sce_sys/trophy")
        if (!trophyRoot.isDirectory) return emptyList()
        return trophyRoot.walkTopDown()
            .filter { it.isFile && it.name.equals(TROPHY_TRP_NAME, ignoreCase = true) }
            .mapNotNull { it.parentFile }
            .distinctBy { it.absolutePath }
            .toList()
    }

    private fun readText(file: File?): String? {
        return file?.takeIf { it.isFile }?.readText(Charsets.UTF_8)
    }

    private fun parseCommunicationId(xml: String): String? {
        var result: String? = null
        parseXml(xml) { parser, event ->
            if (event == XmlPullParser.START_TAG && parser.name == "trophyconf") {
                result = parser.attr("npcommid")
                    .takeIf(String::isNotBlank)
                    ?.takeIf { it.isLikelyTrophyCommunicationId() }
            }
        }
        return result
    }

    private fun String.isLikelyTrophyCommunicationId(): Boolean =
        startsWith("NPWR", ignoreCase = true) && contains("_")

    private fun parseConfigXml(xml: String): TrophyConfig {
        val trophies = mutableListOf<ConfigTrophy>()
        parseXml(xml) { parser, event ->
            if (event == XmlPullParser.START_TAG && parser.name == "trophy") {
                val id = parser.attrInt("id") ?: return@parseXml
                trophies += ConfigTrophy(
                    id = id,
                    groupId = parser.attrInt("gid") ?: 0,
                    grade = parser.attr("ttype").toGrade(),
                    hidden = parser.attr("hidden").equals("yes", ignoreCase = true)
                )
            }
        }
        return TrophyConfig(trophies)
    }

    private fun parseDetailXml(xml: String): TrophyDetails {
        val trophies = mutableMapOf<Int, TextPair>()
        val groups = mutableMapOf<Int, TextPair>()
        var setName = ""
        var setDetail = ""
        var trophyId: Int? = null
        var groupId: Int? = null
        var activeTextTag = ""
        var name = ""
        var detail = ""

        parseXml(xml) { parser, event ->
            when (event) {
                XmlPullParser.START_TAG -> when (parser.name) {
                    "trophy" -> {
                        trophyId = parser.attrInt("id")
                        name = ""
                        detail = ""
                    }
                    "group" -> {
                        groupId = parser.attrInt("id")
                        name = ""
                        detail = ""
                    }
                    "title-name", "title-detail", "name", "detail" -> activeTextTag = parser.name
                }
                XmlPullParser.TEXT -> {
                    val text = parser.text.orEmpty().trim()
                    if (text.isBlank()) return@parseXml
                    when (activeTextTag) {
                        "title-name" -> setName = text
                        "title-detail" -> setDetail = text
                        "name" -> name += text
                        "detail" -> detail += text
                    }
                }
                XmlPullParser.END_TAG -> when (parser.name) {
                    "trophy" -> {
                        trophyId?.let { trophies[it] = TextPair(name, detail) }
                        trophyId = null
                        name = ""
                        detail = ""
                    }
                    "group" -> {
                        groupId?.let { groups[it] = TextPair(name, detail) }
                        groupId = null
                        name = ""
                        detail = ""
                    }
                    "title-name", "title-detail", "name", "detail" -> activeTextTag = ""
                }
            }
        }
        return TrophyDetails(setName, setDetail, groups, trophies)
    }

    private fun parseXml(xml: String, onEvent: (XmlPullParser, Int) -> Unit) {
        val parser = Xml.newPullParser()
        parser.setInput(StringReader(xml))
        var event = parser.eventType
        while (event != XmlPullParser.END_DOCUMENT) {
            onEvent(parser, event)
            event = parser.next()
        }
    }

    private fun XmlPullParser.attr(name: String): String =
        getAttributeValue(null, name).orEmpty()

    private fun XmlPullParser.attrInt(name: String): Int? =
        attr(name).toIntOrNull()

    private fun String.toGrade(): VitaTrophyGrade {
        return when (uppercase()) {
            "P" -> VitaTrophyGrade.Platinum
            "G" -> VitaTrophyGrade.Gold
            "S" -> VitaTrophyGrade.Silver
            "B" -> VitaTrophyGrade.Bronze
            else -> VitaTrophyGrade.Unknown
        }
    }

    private data class ConfigTrophy(
        val id: Int,
        val groupId: Int,
        val grade: VitaTrophyGrade,
        val hidden: Boolean
    )

    private data class TrophyConfig(val trophies: List<ConfigTrophy>)
    private data class TextPair(val name: String, val detail: String)
    private data class InstalledTrophyPackage(
        val communicationId: String,
        val directory: File,
        val game: InstalledVitaGame
    )
    private data class TrophyDetails(
        val setName: String,
        val setDetail: String,
        val groups: Map<Int, TextPair>,
        val trophies: Map<Int, TextPair>
    )

    private class TrpArchive(private val file: File) {
        private val entries: List<TrpEntry>

        init {
            RandomAccessFile(file, "r").use { raf ->
                if (Integer.reverseBytes(raf.readInt()) != TRP_MAGIC) error("Invalid TRP magic")
                raf.seek(0x10)
                val entryCount = raf.readInt()
                val entryInfoOffset = raf.readInt()
                entries = (0 until entryCount).map { index ->
                    raf.seek((entryInfoOffset + index * ENTRY_SIZE).toLong())
                    val filenameBytes = ByteArray(FILENAME_SIZE)
                    raf.readFully(filenameBytes)
                    val filename = filenameBytes
                        .takeWhile { it.toInt() != 0 }
                        .toByteArray()
                        .toString(Charsets.UTF_8)
                    raf.seek((entryInfoOffset + index * ENTRY_SIZE + 0x20).toLong())
                    TrpEntry(
                        filename = filename,
                        offset = raf.readLong(),
                        size = raf.readLong()
                    )
                }
            }
        }

        fun entryNames(): List<String> = entries.map { it.filename }

        fun readTextEntry(name: String): String? =
            runCatching { readEntry(name).toString(Charsets.UTF_8) }.getOrNull()

        fun readEntry(name: String): ByteArray {
            val entry = entries.firstOrNull { it.filename.equals(name, ignoreCase = true) }
                ?: error("Missing TRP entry $name")
            require(entry.size <= Int.MAX_VALUE)
            return RandomAccessFile(file, "r").use { raf ->
                raf.seek(entry.offset)
                ByteArray(entry.size.toInt()).also(raf::readFully)
            }
        }

        private data class TrpEntry(val filename: String, val offset: Long, val size: Long)

        private companion object {
            const val TRP_MAGIC = 0x004DA2DC
            const val ENTRY_SIZE = 0x40
            const val FILENAME_SIZE = 0x20
        }
    }

    private class TrophyProgress(
        private val unlockedIds: Set<Int>,
        private val timestamps: Map<Int, Long>,
        private val grades: Map<Int, VitaTrophyGrade>
    ) {
        fun isUnlocked(id: Int): Boolean = id in unlockedIds
        fun unlockedAt(id: Int): Long? = timestamps[id]?.takeIf { it > 0L }
        fun grade(id: Int): VitaTrophyGrade? = grades[id]?.takeIf { it != VitaTrophyGrade.Unknown }

        companion object {
            val Empty = TrophyProgress(emptySet(), emptyMap(), emptyMap())

            fun read(file: File): TrophyProgress {
                val bytes = file.readBytes()
                val buffer = ByteBuffer.wrap(bytes).order(ByteOrder.LITTLE_ENDIAN)
                if (buffer.remaining() < 4 || buffer.int != TROPHY_USR_MAGIC) return Empty

                val progressFlags = IntArray(FLAG_WORDS) { buffer.nextIntOrZero() }
                repeat(FLAG_WORDS) { buffer.nextIntOrZero() }
                buffer.nextIntOrZero()
                buffer.nextIntOrZero()
                buffer.nextIntOrZero()
                repeat(MAX_GROUPS) { buffer.nextIntOrZero() }

                val timestamps = if (buffer.remaining() >= MAX_TROPHIES * Long.SIZE_BYTES) {
                    LongArray(MAX_TROPHIES) { buffer.nextLongOrZero() }
                } else {
                    LongArray(MAX_TROPHIES)
                }
                val grades = if (buffer.remaining() >= MAX_TROPHIES * Int.SIZE_BYTES) {
                    IntArray(MAX_TROPHIES) { buffer.nextIntOrZero() }
                } else {
                    IntArray(MAX_TROPHIES)
                }

                val unlocked = (0 until MAX_TROPHIES)
                    .filter { id -> progressFlags[id / 32] and (1 shl (id % 32)) != 0 }
                    .toSet()
                return TrophyProgress(
                    unlockedIds = unlocked,
                    timestamps = unlocked.associateWith { timestamps[it] },
                    grades = buildMap {
                        grades.forEachIndexed { index, value ->
                            val grade = when (value) {
                                1 -> VitaTrophyGrade.Platinum
                                2 -> VitaTrophyGrade.Gold
                                3 -> VitaTrophyGrade.Silver
                                4 -> VitaTrophyGrade.Bronze
                                else -> VitaTrophyGrade.Unknown
                            }
                            if (grade != VitaTrophyGrade.Unknown) {
                                put(index, grade)
                            }
                        }
                    }
                )
            }

            private fun ByteBuffer.nextIntOrZero(): Int =
                if (remaining() >= Int.SIZE_BYTES) int else 0

            private fun ByteBuffer.nextLongOrZero(): Long =
                if (remaining() >= Long.SIZE_BYTES) long else 0L

            private const val TROPHY_USR_MAGIC = 0x12D5819A
            private const val MAX_TROPHIES = 128
            private const val FLAG_WORDS = MAX_TROPHIES / 32
            private const val MAX_GROUPS = 16
        }
    }

    private companion object {
        const val TROPHY_TRP_NAME = "TROPHY.TRP"
        const val TROPCONF_NAME = "TROPCONF.SFM"
    }
}
