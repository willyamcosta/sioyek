# Sioyek

Sioyek is a PDF viewer with a focus on textbooks and research papers.

# Fork changes

This is a personal fork of sioyek with the following additions on top of the upstream `development` branch. Everything is off by default and opt-in through `prefs_user.config`.

## Adjacent document navigation

Open the next/previous document in the current folder without going through the file picker. Files are ordered by natural (numeric-aware) filename sort, so `chapter2.pdf` comes before `chapter10.pdf`. Useful for collections split across many files.

Commands (bind them in `keys_user.config`):

```
open_next_document_in_folder <A-.>
open_prev_document_in_folder <A-,>
```

Relevant options (with defaults):

```
auto_open_adjacent_document                  0   # jump to the next/prev file when you scroll past a boundary
preserve_zoom_on_adjacent_document_open      1   # keep zoom/horizontal offset across documents
```

Per-session position memory: when you leave a document via this navigation, sioyek remembers where you were and restores it if you come back during the same session.

Supported file types: pdf, epub, xps, djv(u), fb2, cbz/cbr/cb7/cbt, and common image formats (png, jpg/jpeg, bmp, gif, tif(f), webp, ...).

## Continuous scrolling across adjacent documents

```
continuous_adjacent_document_scroll          0
continuous_adjacent_document_scroll_window   2   # neighbor documents loaded around current (0 = unlimited)
```

When enabled, documents in the current folder are stitched into a continuous vertical scroll using a sliding window. Scrolling off the end of one file flows straight into the next.

As you scroll across a file boundary, the viewer **adopts the document under the viewport as the active document**: the window title, page counter (`current page / that file's page count`), navigation history and saved position all follow the file you are actually looking at. Because the position is saved per file, reopening later restores you to the right document and page — even with continuous scroll disabled — and only files you actually view are recorded in your history and recent files (opening a folder does not pollute history with adjacent files). A status message announces each file as you enter it.

**Sliding window and resource management:**
- To prevent exhausting memory and open file descriptors in large collections (such as manga directories with dozens of volumes), only the current document and $\pm N$ neighbor documents (controlled by `continuous_adjacent_document_scroll_window`, default `2`) are loaded at any time.
- As you scroll deeper into the series, documents behind you slide out of the window and are pruned from memory and the renderer threads, while upcoming documents are seamlessly loaded ahead with zero visual pixel jumps.

**Caveats:**

- It is mutually exclusive with `auto_open_adjacent_document` (continuous scroll takes precedence).
- It disables the fast-coordinates path, so it interacts with two-page mode, selection, links and synctex; treat it as experimental.

## WebP image support

sioyek can open `.webp` images and CBZ archives that contain WebP pages through mupdf's compressed-image pipeline:

- mupdf reads the WebP header for image metadata and retains the compressed bytes until the page needs to be rendered.
- Standalone `.webp` files and WebP pages inside CBZ archives use the same decoder. libwebp writes directly into a mupdf pixmap; there is no intermediate PNG re-encoding.

**Caveats:**

- Builds must use the patched mupdf and link it with libwebp.
- Transparency is preserved by decoding to RGBA and premultiplying alpha in place.

## Manga & Book tracking (AniList & Floppy)

Automatic series identification and reading progress tracking for manga and books, supporting AniList (via GraphQL) and [Floppy](https://github.com/dannyvfilms/Floppy) (self-hosted media tracker via REST API).

**How it works:**
- Sioyek heuristically identifies the series title, volume number, and chapter number from the folder and filename (e.g. `Manga/Chainsaw Man/v08.cbz` -> Title: *Chainsaw Man*, Volume: 8).
- **Opt-in per work:** By default, **no tracking or network sync occurs**. Tracking is disabled until you explicitly check/edit the title and enable tracking for that series.
- **Visual library & shelf view:** View all your tracked books and manga with cover art thumbnails, reading progress (volume/chapter out of total), and status badges (`[Reading]`, `[Completed]`, `[On Hold]`, `[Plan to Read]`, `[Dropped]`). Works are automatically sorted by most recently read.
- **Direct resume reading:** Selecting a work in `:library` instantly resumes reading the exact file and page you last read, or opens the first available volume.
- **Status filtering:** Fast search and filter in the library list by title or status tag (type `reading`, `completed`, `hold`, etc.).
- **Visual cover art:** When searching AniList candidates or browsing library works, Sioyek fetches and caches 2:3 aspect ratio cover thumbnails directly in menus.
- When enabled, crossing chapter or volume boundaries in continuous scroll mode (or opening new chapters) automatically scrobbles your new progress in the background.

Commands (bind in `keys_user.config` or run in command palette):
```
library             # Open visual library/shelf; selecting a series directly resumes reading
manage_library      # Open library with management menu (change status, browse volumes, re-link AniList)
track_work          # Verify/edit title, view cover art, search & link AniList / Floppy, enable tracking
untrack_work        # Disable tracking for the active series
tracking_status     # Display current tracking status, progress, and AniList info
open_anilist        # Instantly open the active work's AniList page in your web browser
open_tracked_works  # Visual library menu (alias for :library)
```

Configuration in `prefs_user.config`:
```
# AniList personal access token (from https://anilist.co/settings/developer)
anilist_token       <your-anilist-bearer-token>

# Floppy self-hosted media tracker (supports Bearer token or Floppy API key)
floppy_url          http://localhost:8080
floppy_token        <your-floppy-app-token>

# Announce detected series on open (1 = on, 0 = silent; tracking remains opt-in)
tracker_auto_notify 1
```

## Nix packaging

A `flake.nix` / `package.nix` build is included. It builds sioyek against a WebP-patched mupdf, so both standalone and CBZ WebP work out of the box:

```
nix build
```

## Planned / Roadmap features

- **Two-page / manga spread continuous scroll**: Support for continuous scrolling in two-page / RTL manga spread modes across volume transitions.

# Development Branch FAQ

## Q: There are build errors with Qt 5.*.

A: If you are building the development branch you need to use Qt 6.7 or 6.8.

## Q: On MacOS I get "sioyek is damaged and cannot be opened. It is recommended to eject the image.".

A: This is related to macOS quarantine. See https://github.com/ahrm/sioyek/discussions/1156#discussioncomment-10822738 .


## Contents
* [Installation](#install)
* [Documentation](#documentation)
* [Video Demo](#feature-video-overview)
* [Features](#features)
* [Build Instructions](#build-instructions)
* [Buy Me a Coffee (or a Book!)](#donation)

## Install
### Official packages
There are installers for Windows, macOS and Linux. See [Releases page](https://github.com/ahrm/sioyek/releases).

### Homebew Cask
There is a homebrew cask available here: https://formulae.brew.sh/cask/sioyek. Install by running:
```
brew install --cask sioyek
```
### Third-party packages for Linux
If you prefer to install sioyek with a package manager, you can look at this list. Please note that they are provided by third party packagers. USE AT YOUR OWN RISK! If you're reporting a bug for a third-party package, please mention which package you're using.

Distro | Link | Maintainer
------- | ----- | -------------
Flathub | [sioyek](https://flathub.org/apps/details/com.github.ahrm.sioyek) | [@nbenitez](https://flathub.org/apps/details/com.github.ahrm.sioyek)
Alpine | [sioyek](https://pkgs.alpinelinux.org/packages?name=sioyek) | [@jirutka](https://github.com/jirutka)
Arch | [AUR sioyek](https://aur.archlinux.org/packages/sioyek) | [@goggle](https://github.com/goggle)
Arch | [AUR Sioyek-git](https://aur.archlinux.org/packages/sioyek-git/) | [@randomn4me](https://github.com/randomn4me)
Arch | [AUR sioyek-appimage](https://aur.archlinux.org/packages/sioyek-appimage/) | [@DhruvaSambrani](https://github.com/DhruvaSambrani)
Debian | [sioyek](https://packages.debian.org/sioyek) | [@viccie30](https://github.com/viccie30)
NixOS | [sioyek](https://search.nixos.org/packages?channel=unstable&show=sioyek&from=0&size=50&sort=relevance&type=packages&query=sioyek) | [@podocarp](https://github.com/podocarp)
openSUSE | [Publishing](https://build.opensuse.org/package/show/Publishing/sioyek) | [@uncomfyhalomacro](https://github.com/uncomfyhalomacro)
openSUSE | [Factory](https://build.opensuse.org/package/show/openSUSE:Factory/sioyek) | [@uncomfyhalomacro](https://github.com/uncomfyhalomacro)
Ubuntu | [sioyek](https://packages.ubuntu.com/sioyek) | [@viccie30](https://github.com/viccie30)


## Documentation
You can view the official documentation [here](https://sioyek-documentation.readthedocs.io/en/latest/).
## Feature Video Overview

[![Sioyek feature overview](https://img.youtube.com/vi/yTmCI0Xp5vI/0.jpg)](https://www.youtube.com/watch?v=yTmCI0Xp5vI)

For a more in-depth tutorial, see this video:

[![Sioyek Tutorial](https://img.youtube.com/vi/RaHRvnb0dY8/0.jpg)](https://www.youtube.com/watch?v=RaHRvnb0dY8)

## Features

### Quick Open

https://user-images.githubusercontent.com/6392321/125321111-9b29dc00-e351-11eb-873e-94ea30016a05.mp4

You can quickly search and open any file you have previously interacted with using sioyek.

### Table of Contents

https://user-images.githubusercontent.com/6392321/125321313-cf050180-e351-11eb-9275-c2759c684af5.mp4

You can search and jump to table of contents entries.

### Smart Jump

https://user-images.githubusercontent.com/6392321/125321419-e5ab5880-e351-11eb-9688-95374a22774f.mp4

You can jump to any referenced figure or bibliography item *even if the PDF file doesn't provide links*. You can also search the names of bibliography items in google scholar/libgen by middle clicking/shift+middle clicking on their name.

### Overview

https://user-images.githubusercontent.com/6392321/154683015-0bae4f92-78e2-4141-8446-49dd7c2bd7c9.mp4

You can open a quick overview of figures/references/tables/etc. by right clicking on them (Like Smart Jump, this feature works even if the document doesn't provide links).

### Mark

https://user-images.githubusercontent.com/6392321/125321811-505c9400-e352-11eb-85e0-ffc3ae5f8cb8.mp4

Sometimes when reading a document you need to go back a few pages (perhaps to view a definition or something) and quickly jump back to where you were. You can achieve this by using marks. Marks are named locations within a PDF file (each mark has a single character name for example 'a' or 'm') which you can quickly jump to using their name. In the aforementioned example, before going back to the definition you mark your location and later jump back to the mark by invoking its name. Lower case marks are local to the document and upper case marks are global (this should be very familiar to you if you have used vim).

### Bookmarks

https://user-images.githubusercontent.com/6392321/125322503-1a6bdf80-e353-11eb-8018-5e8fc43b8d05.mp4

Bookmarks are similar to marks except they are named by a text string and they are all global.

### Highlights


https://user-images.githubusercontent.com/6392321/130956728-7e0a87fa-4ada-4108-a8fc-9d9d04180f56.mp4


Highlight text using different kinds of highlights. You can search among all the highlights.

### Portals (this feature is most useful for users with multiple monitors)



https://user-images.githubusercontent.com/6392321/125322657-41c2ac80-e353-11eb-985e-8f3ce9808f67.mp4

Suppose you are reading a paragraph which references a figure which is not very close to the current location. Jumping back and forth between the current paragraph and the figure can be very annoying. Using portals, you can link the paragraph's location to the figure's location. Sioyek shows the closest portal destination in a separate window (which is usually placed on a second monitor). This window is automatically updated to show the closest portal destination as the user navigates the document.


### Configuration


https://user-images.githubusercontent.com/6392321/125337160-e4832700-e363-11eb-8801-0bee58121c2d.mp4

You can customize all key bindings and some UI elements by editing `keys_user.config` and `prefs_user.config`. The default configurations are in `keys.config` and `prefs.config`.



## Build Instructions

### Linux

#### Fedora

Run the following commands to install dependencies, clone the repository and compile sioyek on Fedora (tested on Fedora Workstation 36).

```
sudo dnf install qt5-qtbase-devel qt5-qtbase-static qt5-qt3d-devel harfbuzz-devel
git clone --recursive --branch development https://github.com/ahrm/sioyek
cd sioyek
./build_linux.sh
``` 

#### Generic distribution
1. Install Qt 5 and make sure `qmake` is in `PATH`.

    Run `qmake --version` to make sure the `qmake` in path is using Qt 5.x.
2. Install `libharfbuzz`:
```
sudo apt install libharfbuzz-dev
```
3. Clone the repository and build:
```
git clone --recursive --branch development https://github.com/ahrm/sioyek
cd sioyek
./build_linux.sh
```

### Windows
1. Install Visual Studio (tested on 2019, other relatively recent versions should work too)
2. Install Qt 5 and make sure qmake is in `PATH`.
3. Clone the repository and build using 64 bit Visual Studio Developer Command Prompt:
```
git clone --recursive --branch development https://github.com/ahrm/sioyek
cd sioyek
build_windows.bat
```

### Mac
1. Uninstall previous Qt6 installed by Homebrew
2. Install Xcode.
3. Install Qt6.
```
pip install aqtinstall
cd /path/to/qt
aqt install-qt mac desktop 6.8.2 clang_64 -m all
export Qt6_DIR=/path/to/qt/6.8.2/macos/
export QT_PLUGIN_PATH=/path/to/qt/6.8.2/macos/plugins
export PKG_CONFIG_PATH=/path/to/qt/6.8.2/macos/lib/pkgconfig
export QML2_IMPORT_PATH=/path/to/qt/6.8.2/macos/qml
export PATH="/path/to/qt/6.8.2/macos/bin:$PATH"
```
4. Clone the repository, build and install:
```
git clone --recursive --branch development https://github.com/ahrm/sioyek
cd sioyek
chmod +x build_mac.sh
setopt PIPE_FAIL PRINT_EXIT_VALUE ERR_RETURN SOURCE_TRACE XTRACE
MAKE_PARALLEL=8 ./build_mac.sh

mv build/sioyek.app /Applications/
sudo codesign --force --sign - --deep /Applications/sioyek.app
```

## Donation
If you enjoy sioyek, please consider donating to support its development.

<a href="https://www.buymeacoffee.com/ahrm" target="_blank"><img src="https://cdn.buymeacoffee.com/buttons/default-orange.png" alt="Buy Me A Coffee" height="41" width="174"></a>
