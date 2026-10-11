# Single-file Windows preview

The preview runs the same C++ engine as the calculator at 320×240, displayed
at 640×480. It does not simulate calculator speed or Ndless hardware.

Place `DDLC-Preview.exe` and a locally generated `.nzp` or `.nzp.tns` pack in the
same folder, then double-click the executable. Pack filenames can be changed.
If several packs are present, select one from the numbered dialog.
No Python, WSL or compiler installation is needed to run the packaged executable.
The pack must match the engine's format. No game assets are embedded in the exe.

Controls: ENTER/SPACE confirm; arrows navigate/scroll; ESC menu/back; H hides
dialogue; S/O save/load; hold CTRL to skip. Type your name and use Backspace
to edit it. Saves use the Windows `.sav` filenames beside the pack, separate
from calculator `.sav.tns` files. Use a writable folder. The preview log is in
`%LOCALAPPDATA%\NspireLiteratureClubPreview\preview.log`.

## Build

Build the `ddlc_preview_engine` CMake target on Windows, or cross-compile with
MinGW-w64 on Linux/WSL. The helper should be named `ddlc-preview-engine.exe`.
For example, with CMake and MinGW-w64 installed in Linux/WSL:

```sh
cmake -S . -B build-preview-win -DCMAKE_SYSTEM_NAME=Windows -DCMAKE_C_COMPILER=x86_64-w64-mingw32-gcc -DCMAKE_CXX_COMPILER=x86_64-w64-mingw32-g++
cmake --build build-preview-win --target ddlc_preview_engine -j4
```

On Windows, install Python with Tk support and run from the repository root:

```powershell
py -3 -m pip install pyinstaller pillow
py -3 -m PyInstaller --noconfirm --clean --onefile --windowed --name DDLC-Preview --add-binary "PATH\TO\ddlc-preview-engine.exe;." tools/desktop-preview/preview.py
```

The output is `dist/DDLC-Preview.exe`. 
