# Device checks

The installer safety tests use disposable cache roots. Cheat card tests use a
Compose host activity and do not change the game library.

`DeviceGameSetupTest` is skipped unless both `gameArchive` and `gameTitleId` are
provided. It installs that authorized archive into the actual app library when
the game is absent, then downloads an explicitly revision-matching cheat pack
only when no pack is already installed. The game and new pack remain available
for gameplay testing. It does not enable cheats or certify their game addresses.

Copy an already obtained archive into the app's external cache so scoped storage
allows the app to read it, then run the test with `am instrument -w -e class
com.sbro.emucorev.DeviceGameSetupTest -e gameArchive <cache path> -e gameTitleId
<title ID> com.sbro.emucorev.test/androidx.test.runner.AndroidJUnitRunner`.
Remove only the temporary cached input after a successful installation.
