This is the early part of the SUSE installation process, before
[YaST](https://en.opensuse.org/Portal:YaST) runs.

See <https://en.opensuse.org/SDB:Linuxrc>.

## Debugging

### Run on Installed System
Linuxrc can run on installed system. It runs in testmode that make debugging easier.
Parameters are passed as common parametrs like: `linuxrc linemode=0 manual=1 LogLevel=8`

### Useful Shortcuts

* ctrl+c then 'q' - exit linuxrc

* ctrl+c then 'c' - change config

* ctrl+c then 'i' - show info detected by linuxrc

* ctrl+c then 's' - start shell

### Logging
To capture log into file with maximum log verbosity use these linuxrc params:
`linuxrc.log=/tmp/linuxrc.log linuxrc.debug=4`

## Testing the Installation

A regular SUSE installation DVD gets built via linuxrc.rpm,
then installation-images.rpm,
then a [KIWI image build](https://build.opensuse.org/package/show/openSUSE:Factory/_product:openSUSE-dvd5-dvd-x86_64).

For testing a shortcut is available: mksusecd
([GitHub](https://github.com/openSUSE/mksusecd),
[OBS](https://build.opensuse.org/package/show/system:install:head/mksusecd)).

Use:

```sh
mksusecd --initrd ./linuxrc.rpm ...
```

or, without an RPM:

```sh
make
mkdir /tmp/initrd
cp linuxrc /tmp/initrd/init
mksusecd --initrd /tmp/initrd ...
```

You may also use `mksusecd --micro` in case you only want to test Stage 1
and not a full install.
