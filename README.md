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
