# NextNotes Flatpak

Application ID: `org.nextks.notes`; executable: `nextnotes`; branch: `stable`.
The first supported architecture is `x86_64`. The app shares
`org.kde.Platform//6.11` with KOS Apps and builds with the matching `org.kde.Sdk`.
Core Qt libraries are supplied by the shared runtime. The editor additionally
needs QtWebEngine, absent from that runtime; `io.qt.qtwebengine.BaseApp//6.11`
supplies that application-specific module and its sandbox support. No host Qt
libraries are copied into `/app`. BaseApp build metadata is retained alongside
the app evidence. Upstream source/build definitions are available at
https://github.com/flathub/io.qt.qtwebengine.BaseApp/tree/branch/6.11.

Run **Build Flatpak** on `main` in GitHub Actions. It pins the application's Git
commit (including its pinned Kos.Ui submodule), records SDK/runtime commits,
builds in the SDK, runs targeted repository/controller regressions, and tests
installation, offline launch, reinstall and data-preserving uninstall in the
disposable runner. Linux uses a kernel-held library lock so a killed Flatpak
process cannot leave a false live-PID lock when the next sandbox reuses its PID.
Reinstall acceptance creates another note after reopening, not merely checking
that the old database file exists. No test or graphical acceptance runs on the developer host.

The resulting `nextnotes-flatpak-x86_64` artifact contains the application and
Sources bundles, a schema-2 `org.nextks.notes.flatpak-release.json`, byte-exact
metadata, environment evidence, and SHA256SUMS. The manifest is read back from
the final bundle with libflatpak; neither online origin nor runtime repository
URLs are embedded. The application has no broad host filesystem access. Files
are selected through portals, and the private library lives under Flatpak's
application-specific XDG data directory. Existing native libraries are not
silently adopted or modified.

After reviewing the successful run, publish its immutable artifacts in the
`NextKde/NkNotes` stable `vX.Y.Z` Release, targeting the recorded source commit.
Add `org.nextks.notes` to `NextKde/appCatalog` and publish a new signed catalog
sequence. Application signatures are optional under the catalog contract;
this entry relies on the approved GitHub repository, SHA-256 and the signed
catalog. Do not add a publisher key unless both the manifest and OSTree commit
have actually been signed by that key.

Runtime dependencies are resolved by the catalog client's approved Flathub
source. The app does not configure runtime remotes or ship a private Qt runtime.
The full source export and third-party notices accompany the binary release.

## Cooperative KWin controls

The bundle includes `Kos.SurfaceShape` from NextKde/NextKde commit
`d7b34a8e8b37a9fb7605333f10d02f7b16e70743`, built against the shared
KDE 6.11 SDK. Its license is installed under `/app/share/licenses/kos-surface-shape`.
`QML_IMPORT_PATH` exposes the module inside the sandbox. Window control slots
reserve actual button geometry; client controls disappear only after KWin
acknowledges takeover. Desktops without this protocol retain working controls.
Release acceptance must exercise the actual bundle under a cooperating Wayland
compositor; an X11 smoke test alone does not establish decoration ownership.
