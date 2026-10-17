## What's new in v1.0.1

This release fixes three bugs that could lose or change text:

- **Unsaved changes in another window could be lost.** New, Open, Close
  Window and dropping a file on a changed document ask to save it first,
  then go on once it is saved. If you went to another window while it
  saved, they went on there: New or Open replaced that window's document,
  throwing away its changes without asking, and Close Window closed it.
- **Shift+Tab could leave a selection you didn't make.** After an empty
  selection (Shift+Right then Shift+Left, or a click with the mouse moved
  a little), Shift+Tab on an indented line left a selection behind, and
  the next key typed deleted text up into the next line.
- **Long searches were cut short.** A search of more than 255 characters
  was shortened without a word, so Replace All also replaced text that
  only matched its beginning.

Also, cedit starts about 10 ms sooner on Linux.

## Downloads

| File | Runs on |
|---|---|
| `cedit-linux-x86_64.tar.gz` | Linux on 64-bit Intel or AMD, with glibc 2.34 or newer (Ubuntu 22.04, Debian 12, Fedora 35, RHEL 9 and later) |
| `cedit-linux-arm64.tar.gz` | Linux on 64-bit ARM, such as a Raspberry Pi 4 or 5 with a 64-bit system, with glibc 2.34 or newer |
| `cedit-macos.dmg` | Any Mac, with Apple silicon or Intel, macOS 11 or newer |
| `cedit-macos-arm64.tar.gz` | The program alone, for a terminal: Macs with Apple silicon (M1 and later), macOS 11 or newer |
| `cedit-macos-x86_64.tar.gz` | The program alone, for a terminal: Macs with an Intel processor, macOS 11 or newer |
| `cedit-windows-x64.zip` | Windows 10 and 11, 64-bit |

### macOS

Open `cedit-macos.dmg` and drag cedit to Applications; it is then with
your other apps, and Finder's **Open With** offers it for text files.

The first time, macOS won't open it, as cedit isn't signed by Apple: on
macOS 15 and later, click **Done**, then in **System Settings** >
**Privacy & Security**, click **Open Anyway** next to "cedit was blocked"
and confirm. On earlier versions, Control-click cedit in Applications,
choose **Open**, then **Open** again. Either way, it is only once.

With cedit.app installed, `open -a cedit file` opens a file from a
terminal. The `.tar.gz` files hold the program alone; downloaded from a
browser, it is blocked the first time, and
`xattr -d com.apple.quarantine cedit` allows it.

### Linux

Each file holds a single program, with nothing to install. Unpack it and
run it:

```sh
tar -xzf cedit-linux-x86_64.tar.gz
./cedit [file]
```

It also holds an entry and an icon for the desktop's application launcher.
To install cedit for yourself with them (`~/.local/bin` has to be on your
`PATH`):

```sh
install -Dm755 cedit ~/.local/bin/cedit
install -Dm644 cedit.desktop ~/.local/share/applications/cedit.desktop
install -Dm644 cedit.svg ~/.local/share/icons/hicolor/scalable/apps/cedit.svg
```

### Windows

Unpack the zip and double-click `cedit.exe`, or run `cedit.exe [file]`
from a terminal. If SmartScreen warns of an unrecognized app, choose
**More info**, then **Run anyway**.
