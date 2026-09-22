package com.sbro.emucorev.core

import java.nio.file.Files
import java.nio.file.Path
import javax.xml.parsers.DocumentBuilderFactory
import kotlin.io.path.readText
import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test

/** Source contracts for the DLC (add-on) install flow. */
class DlcInstallContractTest {
    @Test
    fun installChoiceDialogOffersDlcInstall() {
        val dialog = source("ui/setup/InstallGameChoiceDialog.kt").readText()
        assertTrue(dialog.contains("onInstallDlc"))
        assertTrue(dialog.contains("R.string.install_choice_dlc_title"))
        assertTrue(dialog.contains("R.string.install_choice_dlc_button"))
    }

    @Test
    fun viewModelValidatesDlcBeforeInstalling() {
        val viewModel = source("ui/setup/SetupInstallViewModel.kt").readText()
        assertTrue(viewModel.contains("InstallOperation.Dlc"))
        assertTrue(viewModel.contains("VitaInstallBridge.inspectPkg"))
        assertTrue(viewModel.contains("!info.isDlc"))
        assertTrue(viewModel.contains("R.string.install_dialog_dlc_not_dlc"))
        assertTrue(viewModel.contains("R.string.install_dialog_dlc_wrong_game"))
    }

    @Test
    fun gameDetailMenuExposesDlcInstall() {
        val detail = source("ui/detail/GameDetailScreen.kt").readText()
        assertTrue(detail.contains("onInstallDlc"))
        assertTrue(detail.contains("R.string.detail_install_dlc"))
    }

    @Test
    fun nativeBridgeInspectsPkgHeader() {
        val bridge = app.resolve("src/main/cpp/emucorev/src/vita_install_bridge.cpp").readText()
        assertTrue(bridge.contains("nativeInspectPkg"))
        assertTrue(bridge.contains("content_type == 0x16"))
        val kotlinBridge = source("core/VitaInstallBridge.kt").readText()
        assertTrue(kotlinBridge.contains("nativeInspectPkg"))
        assertTrue(kotlinBridge.contains("KIND_DLC"))
    }

    @Test
    fun everyLocaleContainsTheDlcStrings() {
        val resourceRoot = app.resolve("src/main/res")
        val requiredKeys = dlcKeys(resourceRoot.resolve("values/strings.xml"))
        val localizedDirectories = Files.list(resourceRoot).use { paths ->
            paths.filter {
                it.fileName.toString().startsWith("values-") &&
                    it.fileName.toString() != "values-night"
            }.toList()
        }

        assertEquals(11, localizedDirectories.size)
        assertEquals(10, requiredKeys.size)
        localizedDirectories.forEach { directory ->
            assertEquals(
                "DLC resources differ in ${directory.fileName}",
                requiredKeys,
                dlcKeys(directory.resolve("strings.xml"))
            )
        }
    }

    private fun dlcKeys(path: Path): Set<String> {
        val document = DocumentBuilderFactory.newInstance()
            .newDocumentBuilder()
            .parse(path.toFile())
        val nodes = document.getElementsByTagName("string")
        return buildSet {
            for (index in 0 until nodes.length) {
                val name = nodes.item(index).attributes
                    ?.getNamedItem("name")
                    ?.nodeValue
                    .orEmpty()
                if (name.startsWith("install_dialog_dlc") ||
                    name.startsWith("install_choice_dlc") ||
                    name == "install_dialog_title_dlc" ||
                    name == "detail_install_dlc"
                ) add(name)
            }
        }
    }

    private fun source(path: String): Path =
        app.resolve("src/main/java/com/sbro/emucorev/$path")

    private val app: Path
        get() {
            val workingDirectory = Path.of(System.getProperty("user.dir"))
            return sequenceOf(workingDirectory, workingDirectory.resolve("app"))
                .firstOrNull { Files.isDirectory(it.resolve("src/main")) }
                ?: error("Unable to locate Android app module from $workingDirectory")
        }
}
