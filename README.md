> **Warning**
>
> Hayward is currently a work-in-progress.  The move from i3's tree-of-containers
> model to wmii's list-of-columns is only partly complete, and there are many
> show stopper bugs.

# hayward

Hayward is a hybrid floating/tiling [Wayland] compositor.
It supports arranging manually windows into columns using the keyboard or mouse.
Hayward exists because the primary developer, a long time user of `wmii`, was too lazy to learn a new interaction model.

Hayward was created with [sway] as a starting point and continues to depend on [wlroots].

Goals:
  - Legible - It should be immediately obvious what state hayward is in.
  - Predictable - The effect of a mouse or keyboard operation should be obvious based on current state.
  - Spatial - Windows always stay where you put them so that you can rely on spatial memory to find them again.
  - Fast - Responding to user input should never take more than a frame.  Animations must not block interaction.
  - Discoverable - All operations can be performed using either mouse or keyboard.  Keyboard shortcuts are consistent and documented.
  - Pretty - Hayward aims to be a full-featured (heavy weight) window manager and does not compromise on looks.
  - Robust - Shouldn't crash.  Best in class multi-output support (windows remember positions)

Non-goals:
  - Scripting support - Automatic layout conflicts with legibility, predictability and focus on spatial memory.  User scripting brings instability and slow downs.
  - Configurability - Options that aren't used by developers won't be adequately maintained.
  - Touch screen support - Hayward is a real window manager for real computers.
  - World domination - Hayward exists to make its primary developer happy.

## Installation

### Compiling from Source

Check out [this wiki page][Development setup] if you want to build the HEAD of
hayward and wlroots for testing or development.

Install dependencies:

* meson \*
* [wlroots]
* wayland
* wayland-protocols \*
* pcre2
* json-c
* pango
* cairo
* gdk-pixbuf2 (optional: system tray)
* [scdoc] (optional: man pages) \*
* git (optional: version info) \*

_\* Compile-time dep_

Run these commands:

    meson build/
    ninja -C build/
    sudo ninja -C build/ install

On systems without logind nor seatd, you need to suid the hayward binary:

    sudo chmod a+s /usr/local/bin/hayward

Hayward will drop root permissions shortly after startup.

## Running

Run `hayward` from a TTY. Some display managers may work but are not supported by
hayward (gdm is known to work fairly well).

[sway]: https://github.com/swaywm/sway/
[Wayland]: https://wayland.freedesktop.org/
[Development setup]: https://github.com/bwhmather/hayward/wiki/Development-Setup
[wlroots]: https://gitlab.freedesktop.org/wlroots/wlroots
[scdoc]: https://git.sr.ht/~sircmpwn/scdoc
