# hostname setting in installation/rescue system

The hostname can be set (in order of precedence):

1. via `hostname` boot option
2. via DHCP (either from HOSTNAME or FQDN)
3. Default hostname

linuxrc doesn't write to `/etc/hostname`, but sets the transient hostname
and writes other settings before Yast starts. After that, Yast takes over.
See https://github.com/yast/yast-network/blob/master/doc/hostname.md

## installation system

- `etc/hostname` is an empty file at the beginning of the installation
- `linuxrc` doesn't write anything to `/etc/hostname`
- `linuxrc` writes by default `/etc/install.inf::SetHostname=1`

## If `ifcfg=*=dhcp` is used

- `linuxrc` writes `/etc/sysconfig/network/dhcp:DHCLIENT_SET_HOSTNAME="yes"`

## If `hostname` boot option is used

1. linuxrc writes `/etc/install.inf::SetHostnameUsed=1`
2. linuxrc takes the value of `hostname` boot option and
   - writes it to `/etc/install.inf::Hostname`
   - sets the transient hostname to that value

#### Example

If `hostname=myhost` is used

**/etc/install.inf**
```
[...]
SetHostname: 1
SetHostnameUsed: 1
Hostname: myhost
[...]
```

## If `hostname` boot option is not used

1. `linuxrc` doesn't write `/etc/install.inf::Hostname`, so it is not present.
2. `linuxrc` writes `/etc/install.inf::SetHostnameUsed=0`.
3. `linuxrc` sets the transient hostname to `install`.

#### Example

**/etc/install.inf**
```
[...]
SetHostname: 1
SetHostnameUsed: 0
[...]
```

## rescue system

linuxrc changes the hostname to `rescue`, which may be overridden by user `hostname` boot option.

This value is put into `/etc/hostname` of the rescue system. The value there
may later be overridden by DHCP.

