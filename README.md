<div align="center">
<h1>sandbar</h1>

dwm-like bar for [river](https://github.com/riverwm/river).

![screenshot](/screenshot.png "screenshot")
</div>

## Dependencies
* libwayland-client
* libwayland-cursor
* pixman
* fcft

## Installation


### From Source

```bash
git clone https://github.com/kolunmi/sandbar
cd sandbar
make install
```

### Arch Linux

The package is available on the AUR as [sandbar-git](https://aur.archlinux.org/packages/sandbar-git). Manually install it with `makepkg -sirc` or use your favorite AUR helper:

``` bas
paru -S sandbar-git
```
### NixOS

The package is available on NixOS as **sandbar**. Install it via home-manager, configuration.nix or install it via nix-env:

``` nix
nix-env -iA nixos.sandbar #if your main channel is nixos
```


## Commands
Commands are read through stdin in the following format:
```
output command data
```
where `output` is a wl_output name, "all" to affect all outputs, or "selected" for focused outputs, and `command` and `data` are any one of the following:
| Command             | Data |
|---------------------|------|
| `status`            | text |
| `show`              |      |
| `hide`              |      |
| `toggle-visibility` |      |
| `set-top`           |      |
| `set-bottom`        |      |
| `toggle-location`   |      |

For example, `DP-3 status hello world` would set the status text to "hello world" on output DP-3, if it exists. `all set-top` would ensure all bars are drawn at the top of their respective monitors.

Status text may contain in-line color commands in the following format: `^fg/bg(HEXCOLOR)`.
A color command with no argument reverts to the default value. `^^` represents a single `^` character. Color commands can be disabled with `-no-status-commands`.

## Example Setup

The following setup shows how to spawn both **sandbar** and a **custom status script** that communicates via FIFO with commands running at different intervals.

First, create the file `$HOME/.config/river/bar`:

```bash
#!/usr/bin/env sh

FIFO="$XDG_RUNTIME_DIR/sandbar"
[ -e "$FIFO" ] && rm -f "$FIFO"
mkfifo "$FIFO"

while cat "$FIFO"; do :; done | sandbar \
	-font "Iosevka Nerd Font:Pixelsize" \
	-active-fg-color "#000000" \
	-active-bg-color "#98971a" \
	-inactive-fg-color "#ebdbb2" \
	-inactive-bg-color "#000000" \
	-urgent-fg-color "#000000" \
	-urgent-bg-color "#cc241d" \
	-title-fg-color "#000000" \
	-title-bg-color "#98971a"
```

Then, create the file `$HOME/.config/river/status`:

```bash
#!/bin/env sh

cpu() {
	cpu="$(grep -o "^[^ ]*" /proc/loadavg)"
}

memory() {
	memory="$(free -h | sed -n "2s/\([^ ]* *\)\{2\}\([^ ]*\).*/\2/p")"
}

disk() {
	disk="$(df -h | awk 'NR==2{print $4}')"
}

datetime() {
	datetime="$(date "+%a %d %b %I:%M %P")"
}

bat() {
	read -r bat_status </sys/class/power_supply/BAT0/status
	read -r bat_capacity </sys/class/power_supply/BAT0/capacity
	bat="$bat_status $bat_capacity%"
}

vol() {
	vol="$([ "$(pamixer --get-mute)" = "false" ] && printf "%s%%" "$(pamixer --get-volume)" || printf '-')"
}

display() {
	echo "all status [$memory $cpu $disk] [$bat] [$vol] [$datetime]" >"$FIFO"
}

printf "%s" "$$" > "$XDG_RUNTIME_DIR/status_pid"
FIFO="$XDG_RUNTIME_DIR/sandbar"
[ -e "$FIFO" ] || mkfifo "$FIFO"
sec=0

while true; do
	sleep 1 &
	wait && {
		[ $((sec % 15)) -eq 0 ] && memory
		[ $((sec % 15)) -eq 0 ] && cpu
		[ $((sec % 15)) -eq 0 ] && disk
		[ $((sec % 60)) -eq 0 ] && bat
		[ $((sec % 5)) -eq 0 ] && vol
		[ $((sec % 5)) -eq 0 ] && datetime

		[ $((sec % 5)) -eq 0 ] && display

		sec=$((sec + 1))
	}
done
```

Finally, add these lines to `$HOME/.config/river/init`:

```bash
riverctl spawn "$HOME/.config/river/status"
riverctl spawn "$HOME/.config/river/bar"
```
