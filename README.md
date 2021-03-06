> **Warning**
>
> Hayward is currently a work-in-progress.  The move from i3's tree-of-containers
> model to wmii's list-of-columns is only partly complete, and there are many
> show stopper bugs.

# hayward

Hayward is a [Wayland] compositor which replicates the wmii interaction model.
It started as a fork of [sway].

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
