package com.sbro.emucorev.ui.library

import com.sbro.emucorev.data.InstalledVitaGame
import java.nio.file.Files
import java.nio.file.attribute.FileTime
import java.util.concurrent.CancellationException
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Rule
import org.junit.Test
import org.junit.rules.TemporaryFolder

class LibrarySortingTest {
    @get:Rule val temporary = TemporaryFolder()

    @Test fun titlesIgnoreCaseAndAccentsWithStableTitleIdTies() {
        val games = listOf(game("B", "éclair"), game("Z", "Zoo"), game("A", "Eclair"), game("C", " alpha "))
        assertEquals(listOf("C", "A", "B", "Z"), ids(sortInstalledGames(games, LibrarySortOption.TITLE_ASC)))
        assertEquals(listOf("Z", "B", "A", "C"), ids(sortInstalledGames(games, LibrarySortOption.TITLE_DESC)))
    }

    @Test fun updateTimeSortUsesMetadataAndTitleTieBreaker() {
        val games = listOf(game("A", "Alpha"), game("C", "Charlie"), game("B", "Beta"))
        val metadata = mapOf("A" to LibraryGameSortMetadata(20), "B" to LibraryGameSortMetadata(20), "C" to LibraryGameSortMetadata(10))
        assertEquals(listOf("A", "B", "C"), ids(sortInstalledGames(games, LibrarySortOption.RECENT_DESC, metadata)))
        assertEquals(listOf("C", "A", "B"), ids(sortInstalledGames(games, LibrarySortOption.RECENT_ASC, metadata)))
    }

    @Test fun installedSizeSortUsesLongByteCountsAndStableTies() {
        val games = listOf(game("A", "Alpha"), game("C", "Charlie"), game("B", "Beta"))
        val metadata = mapOf("A" to LibraryGameSortMetadata(sizeBytes = 4_500_000_000), "B" to LibraryGameSortMetadata(sizeBytes = 4_500_000_000), "C" to LibraryGameSortMetadata(sizeBytes = 20))
        assertEquals(listOf("A", "B", "C"), ids(sortInstalledGames(games, LibrarySortOption.SIZE_DESC, metadata)))
        assertEquals(listOf("C", "A", "B"), ids(sortInstalledGames(games, LibrarySortOption.SIZE_ASC, metadata)))
    }

    @Test fun missingMetadataIsZeroAndInputOrderDoesNotChangeTies() {
        val games = listOf(game("B", "Beta"), game("A", "Alpha"))
        assertEquals(listOf("A", "B"), ids(sortInstalledGames(games, LibrarySortOption.SIZE_DESC)))
        assertEquals(listOf("A", "B"), ids(sortInstalledGames(games.reversed(), LibrarySortOption.RECENT_DESC)))
        assertTrue(sortInstalledGames(emptyList(), LibrarySortOption.SIZE_ASC).isEmpty())
    }

    @Test fun unknownStoredChoiceFallsBackToAlphabetical() {
        assertEquals(LibrarySortOption.TITLE_ASC, LibrarySortOption.fromPreferenceValue(null))
        assertEquals(LibrarySortOption.TITLE_ASC, LibrarySortOption.fromPreferenceValue("PLATFORM"))
        assertEquals(LibrarySortOption.SIZE_DESC, LibrarySortOption.fromPreferenceValue("SIZE_DESC"))
    }

    @Test fun metadataCountsNestedInstalledFilesAndKeepsDirectoryUpdateTime() {
        val directory = temporary.newFolder("game").toPath()
        Files.write(directory.resolve("eboot.bin"), ByteArray(11))
        val nested = Files.createDirectories(directory.resolve("sce_sys/package"))
        Files.write(nested.resolve("head.bin"), ByteArray(7))
        Files.setLastModifiedTime(directory, FileTime.fromMillis(1_700_000_000_000))
        val game = game("A", "Alpha").copy(installPath = directory.toString())
        assertEquals(LibraryGameSortMetadata(1_700_000_000_000, 18), readInstalledGameSortMetadata(game, includeSize = true))
        assertEquals(LibraryGameSortMetadata(1_700_000_000_000, 0), readInstalledGameSortMetadata(game, includeSize = false))
    }

    @Test fun deletedInstallFolderDoesNotBreakLibrarySorting() {
        val missing = game("A", "Alpha").copy(installPath = temporary.root.resolve("missing").path)
        assertEquals(LibraryGameSortMetadata(), readInstalledGameSortMetadata(missing, includeSize = true))
    }

    @Test fun metadataIncludesVitaUpdatesAndDlcBesideTheBaseInstall() {
        val base = temporary.newFolder("vita", "ux0", "app", "BASE_FOLDER").toPath()
        val ux0 = base.parent.parent
        val patch = Files.createDirectories(ux0.resolve("patch/PCSA00001"))
        val dlc = Files.createDirectories(ux0.resolve("addcont/PCSA00001/DLC001"))
        Files.write(base.resolve("eboot.bin"), ByteArray(5))
        Files.write(patch.resolve("eboot.bin"), ByteArray(7))
        Files.write(dlc.resolve("data.bin"), ByteArray(9))
        Files.setLastModifiedTime(base, FileTime.fromMillis(1_700_000_000_000))
        Files.setLastModifiedTime(patch, FileTime.fromMillis(1_700_000_030_000))
        Files.setLastModifiedTime(dlc.parent, FileTime.fromMillis(1_700_000_020_000))
        val game = game("PCSA00001", "Patched game").copy(installPath = base.toString())
        assertEquals(LibraryGameSortMetadata(1_700_000_030_000, 0), readInstalledGameSortMetadata(game, includeSize = false))
        assertEquals(LibraryGameSortMetadata(1_700_000_030_000, 21), readInstalledGameSortMetadata(game, includeSize = true))
    }

    @Test(expected = CancellationException::class)
    fun sizeWalkHonorsCancellationInsteadOfPublishingAStaleSort() {
        val directory = temporary.newFolder("cancelled")
        readInstalledGameSortMetadata(game("A", "Alpha").copy(installPath = directory.path), includeSize = true) {
            throw CancellationException("Sort choice changed")
        }
    }

    private fun ids(games: List<InstalledVitaGame>) = games.map { it.titleId }
    private fun game(id: String, title: String) = InstalledVitaGame(id, title, null, null, null, null, null, "/games/$id")
}
