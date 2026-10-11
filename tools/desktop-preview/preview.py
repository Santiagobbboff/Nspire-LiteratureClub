import pathlib
import os
import queue
import subprocess
import sys
import threading
import time
import tkinter as tk
from tkinter import messagebox, simpledialog
from PIL import Image, ImageTk

# Frozen executables load packs beside the exe, never from PyInstaller's
# temporary extraction directory or the shell's current working directory.
HERE = pathlib.Path(sys.executable if getattr(sys, 'frozen', False) else __file__).resolve().parent
BUNDLE = pathlib.Path(getattr(sys, '_MEIPASS', pathlib.Path(__file__).resolve().parent))
root = tk.Tk()
root.withdraw()
packs = sorted((p for p in HERE.iterdir() if p.is_file() and
                p.name.lower().endswith(('.nzp', '.nzp.tns'))), key=lambda p: p.name.lower())
if not packs:
    messagebox.showerror('Content pack missing', 'Place a generated .nzp or .nzp.tns content pack in the same folder as this executable.\n\n'+str(HERE))
    root.destroy()
    sys.exit(1)
if len(packs) == 1:
    pack = packs[0]
else:
    selection = simpledialog.askinteger('Choose a content pack',
        '\n'.join(f'{i+1}: {p.name}' for i,p in enumerate(packs)),
        minvalue=1, maxvalue=len(packs), parent=root)
    if selection is None:
        root.destroy()
        sys.exit(0)
    pack = packs[selection-1]

bridge = BUNDLE / 'ddlc-preview-engine.exe'
if not bridge.is_file():
    messagebox.showerror('Preview engine missing', 'The bundled Windows engine could not be found.')
    root.destroy()
    sys.exit(1)
root.title('DDLC — desktop runtime preview')
root.resizable(False, False)
screen = tk.Label(root, width=640, height=480, bg='black')
screen.pack()
tk.Label(root, text='ENTER / SPACE: advance  •  Arrows: navigate  •  ESC: menu\nH: hide text  •  S/O: save/load  •  Hold CTRL: skip', pady=7).pack()
commands = queue.Queue()
frames = queue.Queue(maxsize=1)
stopped = threading.Event()
control = False
log_dir = pathlib.Path(os.environ.get('LOCALAPPDATA', HERE)) / 'NspireLiteratureClubPreview'
log_dir.mkdir(parents=True, exist_ok=True)
log = open(log_dir / 'preview.log', 'ab', buffering=0)
try:
    # Restore Windows' normal DLL search for the statically linked helper;
    # it does not need libraries from the bundled Python runtime.
    import ctypes
    ctypes.windll.kernel32.SetDllDirectoryW(None)
    process = subprocess.Popen([str(bridge), str(pack)], cwd=HERE,
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=log,
        creationflags=subprocess.CREATE_NO_WINDOW)
except OSError as error:
    messagebox.showerror('Preview launch failed', str(error))
    root.destroy()
    log.close()
    sys.exit(1)

def key(event):
    global control
    if event.keysym in ('Control_L', 'Control_R'):
        control = True
        return
    mapping = {'Return':'e', 'space':'e', 'Escape':'x', 'Up':'u', 'Down':'d',
               'Left':'l', 'Right':'r', 'BackSpace':'b'}
    command = mapping.get(event.keysym)
    if command:
        commands.put(command)
    elif event.char and 32 <= ord(event.char) <= 126:
        commands.put('char '+str(ord(event.char)))

def release(event):
    global control
    if event.keysym in ('Control_L', 'Control_R'): control = False

def close():
    stopped.set()
    try: process.stdin.close()
    except (OSError, ValueError): pass
    root.destroy()

def read_exact(count):
    data = bytearray()
    while len(data) < count:
        chunk = process.stdout.read(count-len(data))
        if not chunk: raise RuntimeError('The content pack could not be loaded or the preview stopped.\n\nDetails: '+str(log_dir / 'preview.log'))
        data.extend(chunk)
    return bytes(data)

def publish(item):
    try: frames.get_nowait()
    except queue.Empty: pass
    frames.put_nowait(item)

def worker():
    previous = time.monotonic()
    try:
        while not stopped.is_set():
            now = time.monotonic()
            pending = ['tick '+str(min(250, max(1, int((now-previous)*1000))))]
            previous = now
            while True:
                try: pending.append(commands.get_nowait())
                except queue.Empty: break
            if control: pending.append('f')
            pending.append('p')
            process.stdin.write(('\n'.join(pending)+'\n').encode('ascii'))
            process.stdin.flush()
            if read_exact(1) == b'\x01':
                publish(None)
                return
            publish(read_exact(320*240*3))
            stopped.wait(max(0, 0.033-(time.monotonic()-now)))
    except Exception as error:
        if not stopped.is_set(): publish(str(error))

def refresh():
    try:
        data = frames.get_nowait()
        if data is None:
            close()
            return
        if isinstance(data, str):
            messagebox.showerror('Preview error', data)
            close()
            return
        image = Image.frombytes('RGB', (320,240), data).resize((640,480), Image.Resampling.NEAREST)
        screen.image = ImageTk.PhotoImage(image)
        screen.configure(image=screen.image)
    except queue.Empty: pass
    root.after(16, refresh)

root.bind('<KeyPress>', key)
root.bind('<KeyRelease>', release)
def focus_out(event):
    global control
    control = False
root.bind('<FocusOut>', focus_out)
root.protocol('WM_DELETE_WINDOW', close)
if len(sys.argv) == 3 and sys.argv[1] == '--capture':
    # Capture one real runtime frame, also useful for checking a packaged build.
    process.stdin.write(b'p\n')
    process.stdin.flush()
    if read_exact(1) != b'\x00': raise RuntimeError('No frame available')
    Image.frombytes('RGB', (320,240), read_exact(320*240*3)).save(sys.argv[2])
    process.stdin.write(b'q\n')
    process.stdin.flush()
    process.wait(timeout=5)
    root.destroy()
    log.close()
    sys.exit(0)
root.deiconify()
threading.Thread(target=worker, daemon=True).start()
root.after(16, refresh)
root.mainloop()
try: process.wait(timeout=3)
except subprocess.TimeoutExpired: process.terminate()
log.close()
