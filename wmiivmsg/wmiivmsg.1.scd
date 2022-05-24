wmiivmsg(1)

# NAME

wmiivmsg - Send messages to a running instance of wmiiv over the IPC socket.

# SYNOPSIS

_wmiivmsg_ [options...] [message]

# OPTIONS

*-h, --help*
	Show help message and quit.

*-m, --monitor*
	Monitor for responses until killed instead of exiting after the first
	response. This can only be used with the IPC message type _subscribe_. If
	there is a malformed response or an invalid event type was requested,
	wmiivmsg will stop monitoring and exit.

*-p, --pretty*
	Use pretty output even when not using a tty.
	Not available for all message types.

*-q, --quiet*
	Sends the IPC message but does not print the response from wmiiv.

*-r, --raw*
	Use raw JSON output even if using a tty.

*-s, --socket* <path>
	Use the specified socket path. Otherwise, wmiivmsg will ask wmiiv where the
	socket is (which is the value of $WMIIVSOCK, then of $I3SOCK).

*-t, --type* <type>
	Specify the type of IPC message. See below.

*-v, --version*
	Print the version (of wmiivmsg) and quit.

# IPC MESSAGE TYPES

*<command>*
	The message is a wmiiv command (the same commands you can bind to keybindings
	in your wmiiv config file). It will be executed immediately.

	See *wmiiv*(5) for a list of commands.

	_wmiivmsg_ can return pretty printed (standalone-default) or JSON-formatted
	(*--raw*) output. For detailed documentation on the returned JSON-data of
	each message type listed below,	refer to *wmiiv-ipc*(7). The JSON-format can
	contain more information than the pretty print.

	Tips:
	- Command expansion is performed twice: once by wmiivmsg, and again by wmiiv.
	  If you have quoted multi-word strings in your command, enclose the entire
	  command in single-quotes. For example, use
	  _wmiivmsg 'output "Foobar Display" enable'_ instead of
	  _wmiivmsg output "Foobar Display" enable_. Furthermore, note that comma
	  separated options also count as multi-word strings, because commas can be
	  used to execute commands on the same line.
	- If you are providing a command that contains a leading hyphen (_-_), insert
	  two hyphens (_--_) before the command to signal to wmiivmsg not to parse
	  anything beyond that point as an option. For example, use
	  _wmiivmsg -- mark --add test_ instead of _wmiivmsg mark --add test_.

*get\_workspaces*
	Gets a list of workspaces and their status.

*get\_inputs*
	Gets a list of current inputs.

*get\_outputs*
	Gets a list of current outputs.

*get\_tree*
	Gets a JSON-encoded layout tree of all open windows, containers, outputs,
	workspaces, and so on.

*get\_seats*
	Gets a list of all seats,
	its properties and all assigned devices.

*get\_marks*
	Get a JSON-encoded list of marks.

*get\_bar\_config*
	Get a JSON-encoded configuration for wmiivbar.

*get\_version*
	Get version information for the running instance of wmiiv.

*get\_binding\_modes*
	Gets a JSON-encoded list of currently configured binding modes.

*get\_binding\_state*
	Gets JSON-encoded info about the current binding state.

*get\_config*
	Gets a copy of the current configuration. Doesn't expand includes.

*send\_tick*
	Sends a tick event to all subscribed clients.

*subscribe*
	Subscribe to a list of event types. The argument for this type should be
	provided in the form of a valid JSON array. If any of the types are invalid
	or if a valid JSON array is not provided, this will result in a failure.

# RETURN CODES

*0*
	Success

*1*
	wmiivmsg errors such as invalid syntax, unable to connect to the ipc socket
	or unable to parse wmiiv's reply

*2*
	WMiiv returned an error when processing the command (ex. invalid command,
	command failed, and invalid subscription request)

# SEE ALSO

*wmiiv*(5) *wmiiv-bar*(5) *wmiiv-input*(5) *wmiiv-output*(5) *wmiiv-ipc*(7)
