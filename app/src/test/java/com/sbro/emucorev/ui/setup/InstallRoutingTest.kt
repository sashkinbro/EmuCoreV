package com.sbro.emucorev.ui.setup

import com.sbro.emucorev.core.InstallerArchiveFormat
import com.sbro.emucorev.core.VitaEmbeddedPackageSupport
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

class InstallRoutingTest {
    @Test fun installableZipGoesStraightToTheNativeInstaller() {
        // VPK or app/<TITLE>/sce_sys/param.sfo dump: no staging copy, no repack.
        assertFalse(SetupInstallViewModel.shouldExpandBeforeInstall(InstallerArchiveFormat.ZIP, directInstallableZip = true))
    }

    @Test fun otherContainersAndZipsWithoutMetadataAreExpandedFirst() {
        assertTrue(SetupInstallViewModel.shouldExpandBeforeInstall(InstallerArchiveFormat.ZIP, directInstallableZip = false))
        assertTrue(SetupInstallViewModel.shouldExpandBeforeInstall(InstallerArchiveFormat.SEVEN_Z, directInstallableZip = false))
        assertTrue(SetupInstallViewModel.shouldExpandBeforeInstall(InstallerArchiveFormat.RAR5, directInstallableZip = false))
    }

    @Test fun unknownFilesAreNotExpanded() {
        assertFalse(SetupInstallViewModel.shouldExpandBeforeInstall(null, directInstallableZip = false))
    }

    @Test fun headBinInsideSceSysIsContentMetadataNotAPackage() {
        assertTrue(VitaEmbeddedPackageSupport.isInsideContentMetadata("Game.zip/app/PCSA00011/sce_sys/package/head.bin"))
        assertTrue(VitaEmbeddedPackageSupport.isInsideContentMetadata("app\\PCSA00011\\SCE_SYS\\package\\head.bin"))
        assertFalse(VitaEmbeddedPackageSupport.isInsideContentMetadata("Game.zip/UP0001-PCSA00011_00-0000000000000000.pkg"))
        assertFalse(VitaEmbeddedPackageSupport.isInsideContentMetadata("sce_sys.pkg"))
    }
}
