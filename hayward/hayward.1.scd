hayward(1)

# NAME

hayward - An i3-compatible Wayland compositor

# SYNOPSIS

*hayward* [options...] [command]

# OPTIONS

*-h, --help*
	Show help message and quit.

*-c, --config* <config>
	Specifies a config file.

*-C, --validate*
	Check the validity of the config file, then exit.

*-d, --debug*
	Enables full logging, including debug information.

*-v, --version*
	Show the version number and quit.

*-V, --verbose*
	Enables more verbose logging.

*--get-socketpath*
	Gets the IPC socket path and prints it, then exits.

# DESCRIPTION

hayward was created to fill the need of an i3-like window manager for Wayland. The
upstream i3 developers have no intention of porting i3 to Wayland, and projects
proposed by others ended up as vaporware. Many thanks to the i3 folks for
providing such a great piece of software, so good that your users would rather
write an entirely new window manager from scratch that behaved _exactly_ like i3
rather than switch to something else.

You can run hayward directly from a tty, or via a Wayland-compatible login manager.

# CONFIGURATION

hayward searches for a config file in the following locations, in this order:

. ~/.hayward/config
. $XDG_CONFIG_HOME/hayward/config (suggested location)
. ~/.i3/config
. $XDG_CONFIG_HOME/i3/config
. /etc/hayward/config
. /etc/i3/config

If unset, $XDG_CONFIG_HOME defaults to *~/.config*.

An error is raised when no config file is found. The recommended default
configuration is usually installed to */etc/hayward/config*; you are encouraged to
copy this to *~/.config/hayward/config* and edit it from there.

For information on the config file format, see *hayward*(5).

# IPC COMMANDS

Though *haywardmsg*(1) is generally preferred, you may run *hayward* _command_ to
send _command_ to the running instance of hayward. You can also issue commands
with *i3-msg*(1) or even with *i3*(1).

# ENVIRONMENT

The following environment variables have an effect on hayward:

_HAYWARDSOCK_
	Specifies the path to the hayward IPC socket.

_XKB\_DEFAULT\_RULES_, _XKB\_DEFAULT\_MODEL_, _XKB\_DEFAULT\_LAYOUT_,
_XKB\_DEFAULT\_VARIANT_, _XKB\_DEFAULT\_OPTIONS_
	Configures the xkb keyboard settings. See *xkeyboard-config*(7). The
	preferred way to configure the keyboard is via the configuration file, see
	*hayward-input*(5).

The following environment variables are set by hayward:

_DISPLAY_
	If compiled with Xwayland support and Xwayland is not disabled by the
	config, this will be set to the name of the X display used for Xwayland.

_I3SOCK_
	For compatibility with i3, specifies the path to the hayward IPC socket.

_HAYWARDSOCK_
	Specifies the path to the hayward IPC socket.

_WAYLAND_DISPLAY_
	Specifies the name of the Wayland display that hayward is running on.

_XCURSOR_SIZE_
	Specifies the configured cursor size.

_XCURSOR_THEME_
	Specifies the configured cursor theme.

# AUTHORS

Maintained by Simon Ser <contact@emersion.fr>, who is assisted by other open
source contributors. For more information about hayward development, see
<https://github.com/haywardwm/hayward>.

# SEE ALSO

*hayward*(5) *haywardmsg*(1) *hayward-input*(5) *hayward-output*(5) *hayward-bar*(5)
*hayward-ipc*(7)
