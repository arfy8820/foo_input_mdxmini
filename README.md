# MDX input for foobar2000

MDX input 0.1.0 decodes Sharp X68000 MDX music using mdxmini. It emulates the Yamaha YM2151 OPM and mixes ADPCM or PCM samples from companion PDX banks. It supports eight FM tracks and one or eight sample tracks, depending on the file.

Install the unified x86 and x64 [GitHub release](https://github.com/arfy8820/foo_input_mdxmini/releases/latest) through **Preferences → Components → Install** and restart. Add the `.mdx` file to a playlist. PDX banks are supporting files. Put the named bank beside the MDX file or set a fallback sample folder in Preferences. A missing bank leaves the sample channels silent and is reported in Properties and the mixer.

## Settings and mixer

- **Preferences → Playback → Decoding → MDX (mdxmini)** provides sample rate, loop count, loop fade speed, YM2151 gain, PDX gain, mono output and a fallback PDX folder. Apply saves the settings. Reopen the track to use them.
- **View → MDX channel mixer** opens the live mixer while an MDX file is playing. Tab to **Sound channel** and use the arrows or Alt+Down. Tab to **Mute selected channel** and press Space to toggle it.
- **Solo selected** isolates the selected channel until cleared. Individual mute choices are restored when solo is cleared. **Unmute all** clears both. The read-only **Channel activity** field shows the latest decoded note or sample index. The text holds steady while focused.
- Mixer changes affect the current interactive decoder. They can take effect after already-buffered audio plays and are cleared when a new decoder starts. Conversion and verification use the saved playback settings.

The component reads Shift-JIS MDX titles and supports seeking by rendering from the start. It follows the upstream 20-minute duration ceiling and does not write to MDX or PDX files.

## Build

Use Visual Studio C++ Build Tools and the [foobar2000 SDK 2026-10-01](https://www.foobar2000.org/SDK) in the adjacent `SDK-2026-10-01` folder. The project includes the mdxmini sources it compiles.

```powershell
.\build.ps1
.\build.ps1 -Platform Win32 -FoobarDirectory 'C:\Program Files (x86)\foobar2000'
.\package.ps1
```

The engine test generates original MDX/PDX fixtures. The UI test uses `shared.dll` from foobar2000 of the same architecture and checks native accessible names, keyboard selection and mute/solo behavior. The packages are written under `dist/`. Builds using v145 target Windows 10 or newer.

After building, run `test-host.ps1 -FoobarDirectory <player directory>` for a decoder integration test in an isolated foobar2000 profile. Pass `-Platform Win32` to test the x86 package with a 32-bit player.

The x64 and x86 builds passed the decoder, Windows accessibility and isolated foobar2000 tests with generated fixtures. Live NVDA operation and a wider collection of MDX files remain untested.

## Upstream

The vendored [mdxmini](https://github.com/mistydemeo/mdxmini) source is pinned to commit `003531a471c1955f4ed4357d0e2a6cba809c34a0`. The component uses the upstream sequencer and synth cores with local changes for in-memory input, per-channel mute, bounded parsing and buffer cleanup. See [upstream credits](vendor/mdxmini/AUTHORS) and [GPL terms](vendor/mdxmini/COPYING).

The statically linked MDX component is distributed under the GNU General Public License version 2. The source and build scripts corresponding to each release are available from its Git tag. The foobar2000 SDK is available separately under its own license.
