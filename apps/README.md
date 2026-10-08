# Native bob32 applications

Build an application from the repository root:

```text
gcc -E -P -nostdinc -undef -I kernel apps/notes.c -o build/notes.i
build/bobcc build/notes.i build/notes.basm build/notes.b32 --wide-app
```

Use the same two build commands with `apps/graphics_demo.c` and the
`build/graphics_demo` output names to build the interactive canvas example.
Import it once as `graphics`, then run `run graphics`; it waits for a key and
returns to the shell.

Build `apps/gui_demo.c` the same way to try the desktop prototype. The larger
editor starts with `new.txt` and `bob!` in a filename field and multiline text
area. Click either field to place the caret, or use Tab to cycle through the
filename, text-area, and file-browser focus. In the browser, select a text file
and press `e` to load it; Ctrl+S saves the text back to the guest filesystem.
Enter inserts a newline. Escape returns to the shell. On a Windows console,
click a window to raise and focus it or drag its title bar to move it.
`kernel/bob_wm.h` is the shared window manager used by the GUI demo. It owns
fixed-capacity window records, focus, stacking, hit testing, title-bar drag
capture, event routing, screen clamping, and dirty redraw tracking. Apps provide
the manager's fixed storage arrays and use `bob_wm_dispatch` to route an event;
the returned window ID identifies its recipient, and mouse coordinates in the
routed event are window-local. Call `bob_wm_invalidate` when application content
changes, then draw IDs returned by `bob_wm_next_dirty` in stacking order.
`kernel/bob_gui.h` contains reusable window-frame, button, menu, single-line
text-field, and multiline text-area widgets. Drawn windows have framed borders
and a distinct title strip.
The `bob_gui_button_draw` and `bob_gui_button_event` helpers provide a reusable
clickable button with hover and pressed states. The demo button focuses Window
B on a left click, and the bob32 test suite exercises hit-testing, dragging,
left-button activation, and release-outside behavior with synthetic events.
Overlapping-window hit-testing and raising are also tested.
The text-field helpers support click-to-position, insertion, Backspace/Delete,
arrow/Home/End navigation, caret drawing, and horizontal scrolling. The text
area supports multiline insertion, vertical movement with column preservation,
click-to-position, caret visibility, and mouse-wheel scrolling. Native bob32
fixtures check text and cursor state, including wheel-scroll bounds.
The popup menu has fixed-stride labels, keyboard selection, Enter/Escape, and
mouse hover/click activation. Window B opens it from the File title area or
with `m`; its actions open the selected text file, start delete confirmation,
or close the menu. Popup placement follows Window B when it is dragged and
clamps the menu inside the 80x25 screen; widget tests cover edge and invalid
screen placements.
Window B lists guest filesystem entries and previews the selected text file;
`n`/`p` and mouse clicks change the selection, while Up/Down or `u`/`d` scroll
the preview. On Windows, the mouse wheel scrolls the editor or preview and
changes file selection when over the list. Press `x` to open an
on-screen Yes/Cancel prompt; click a button
or press `y`/`n` to confirm or cancel.
The GUI integration test creates a long `gui.txt`, scrolls beyond the initial
preview, checks a unique trailing marker, confirms deletion, and verifies the
file can no longer be read. It also creates `new.txt` from the GUI, snapshots
the guest filesystem, restores it in a second emulator process, and reads the
saved contents with `cat`. A separate integration test loads a two-line text
file, edits and saves it through the GUI, then checks the saved newline and text
with `cat`.

Start the bob32 OS, import the image, and use it from the guest filesystem:

```text
import32 build/notes.b32 notes
run notes put journal "bob! a saved note"
run notes edit journal "bob! updated note"
run notes add journal "another line"
run notes show journal
ls
save
```

`notes edit NAME [TEXT]` creates or replaces a text note, while
`notes add NAME [TEXT]` appends a line (or prompts for one). `notes` protects
binary and executable files from note-specific commands. It also supports
`run notes delete journal`. The `echo` app prints its
arguments, `ls` lists guest files and their kinds/sizes, and `cat FILE` displays
a text file. `sysinfo` reports application memory, filesystem usage, display
dimensions, and keyboard/mouse availability. Import
each image once; the application and its data are preserved by B32S snapshots.
`cat FILE` preserves existing trailing newlines and only adds a line break when
nonempty file contents have no final newline.

Build `apps/launcher.c` with the same commands to list stored native apps or
start one with arguments. For example, `run launcher echo "a saved note"`
starts `echo` as a child app and reports its exit code. Native apps can call
`bob_app_run(name, arguments, &exit_code)` from `bob_process.h` directly.
The GUI demo also has a small application-launcher panel: focus Window B and
press `a`, choose a stored app with `n`/`p` or the arrow keys, and press `r` or
click Launch. The desktop resumes and displays the child exit status.
Press `?` for a bitmap-font help overlay with the desktop shortcuts; Escape or
its Close button returns to the desktop. Launcher and help panels clear the
desktop canvas behind them so their contents remain readable.

`graphics_demo` uses the version 1 `bob_gfx.h` canvas API. Its first backend is
an 80x25 character-cell framebuffer with colors, filled rectangles, lines,
text, transparent bitmap cells (including per-cell colors), and explicit presentation. The same app can
later target a pixel renderer without changing its draw calls.
Include `bob_font.h` when an app needs the opt-in 5x7 bitmap text renderer.
The demo waits through `bob_event.h`, a blocking event interface that currently
normalizes typed characters, special-key presses/releases, mouse movement, and
mouse-button transitions when the Windows console provides those records.

Apps use `bob_fs.h` for guest file operations and `bob_string.h` for string and
number helpers. The filesystem service has a versioned request protocol and
keeps guest RAM tables private from application code.
`bob_event.h` provides blocking wait and nonblocking poll operations, while
`bob_event_queue.h` provides an optional caller-owned FIFO when an app needs to
buffer or defer events. A full queue reports overflow without dropping an
earlier event.
