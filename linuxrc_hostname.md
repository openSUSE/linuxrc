# hostname setting in installation/rescue system

There are 3 cases:

1. no hostname is set

2. hostname is set via dhcp (either from HOSTNAME or FQDN)

3. hostname is set via `hostname` boot option

linuxrc defaults to `install`, which may be overridden by dhcp (2) or user
(3). (3) takes precedence over (2).

## installation system

When yast is started, the value from (3) is passed to yast via
`/etc/install.inf::Hostname` and ends up in `/etc/hostname` of the installed system.

When linuxrc doesn't set `/etc/install.inf::Hostname` yast generates a
default `/etc/hostname` entry of the form `linux-XXXX` (+ `.suse` in sle12).
`XXXX` is some random part.

There is no `/etc/hostname` file in the installation environment.

## rescue system

linuxrc changes the hostname to `rescue`, which may be overridden by user (3).

This value is put into `/etc/hostname` of the rescue system. The value there may later
be overridden by dhcp (2).

