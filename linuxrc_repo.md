# specifying installation repositories in linuxrc

## 1. SUSE tags install media

*(repo meta data are in /suse/setup/descr/)*

:warning: *Note: This is obsolete since SLE15/openSUSE Leap 15, the newer
products use the repomd repositories on the installation media, see below.*

linuxrc identifies this repo by checking for a file '/content' in the
installation repository. The file must have a valid signature
('/content.asc'). linuxrc parses this file for sha* digests (the `HASH` lines).
These digests are used to verify any files linuxrc accesses from the
installation repository.

To specify the installation repository, use the `install` option:

```sh
install=ftp://example.com/foo
```

linuxrc also loads a file system image containing the installation system
(`YaST`) from the repository. The default location is `boot/<ARCH>/root`.

In the example above on `x86_64` this would be `ftp://example.com/foo/boot/x86_64/root`.)

It is possible to specify a different location (e.g. in case the
installation system is not part of the repository) using the `instsys`
option. Either as a separate option or appended to the `install` url:

```sh
install=ftp://example.com/foo instsys=disk:/bar/root

# is the same as
install=ftp://example.com/foo?instsys=disk:/bar/root
```

`instsys` can be a relative url (a plain file name) which is then interpreted as relative
to the `install`-url:

```sh
install=ftp://example.com/foo instsys=bar/root

# is the same as
install=ftp://example.com/foo instsys=ftp://example.com/foo/bar/root
```

Note that even though `instsys` can point anywhere, the files downloaded
from there must still match the digests obtained from `/content` from the repository location.

Note also, that the full path as specified in `content` must appear in the
url. For example, if `content` provides a sha256 digest for `boot/x86_64/root` this will not be applied to
`foo/root`.


## 2. plain repomd repository

*(repo meta data are in /repodata/)*

linuxrc identifies this repo by checking for a file '/repodata/repomd.xml' in
the installation repository. This file's signature is not checked (linuxrc
does not parse this file).


### 2.1. getting file digests

There is no longer a `content` file. Instead, there is now a `CHECKSUMS`
file (with sligthly different format) containing sha256 digests of
repository files.

`CHECKSUMS` must be signed (detached signature in `CHECKSUMS.ASC`).

linuxrc gets the digests from two locations:
- a `content` file included in the initrd at installation system build time (placed directly into linuxrc's config directory)
- `CHECKSUMS` from the repo location

To include `content` in the initrd in a place linuxrc finds it, do something like:

```sh
mkdir -p /tmp/foo/etc/linuxrc.d
cp content /tmp/foo/etc/linuxrc.d/
cd /tmp/foo
find . | cpio --create --format=newc | xz --check=crc32 --to-stdout >>initrd_on_boot_medium
```

## 3. Multi-repository medium

A multi-repository medium contains several repositories in subdirectories
indexed in the `/media.1/products` file. Each subdirectory contains
a separate repository with own metadata.

In that case linuxrc saves the URL path to the root directory into
the `ZyppRepoURL` value in `/etc/install.inf` file. YaST will detect
multiple subdirectories and handle that in a special way.


## 4. No installation repository

In some special cases (PXE boot) it is possible to start the installation without
any installation repository. In that case YaST uses an integrated installation
repository present in the inst-sys.

Use the `no_repo=1` boot option to activate this mode. In this case linuxrc does
not save the `ZyppRepoURL` value into the `/etc/install.inf` file.


## 5. components linuxrc reads

linuxrc reads files from two distinct locations:

1. the installation system ('inst-sys')
2. the installation repository ('repo')

The repo location is specified with the `install` option. Optionally, the inst-sys location is specified
with the `instsys` option. If it's not given, it is implicitly assumed to be `boot/<ARCH>/root`, relative to the
repo location.

See the previous sections for examples.

### 5.1. files read from inst-sys location

linuxrc replaces the last path component from the location url with `config`
to get the url of a config file and tries to read it.

If the config file is *not* there, linuxrc will continue to read the inst-sys image as originally specified and mount it.

If the config file was found (the normal case) it is parsed to determine the
components needed for the inst-sys and then the components are loaded.

A simple config file may look like:

```sh
# boot/x86_64/config

root:   common root bind
rescue: common rescue

```

Meaning that the `root` image consists of three files `common`, `root`, and `bind` and the `rescue` image of two parts
`common` and `rescue`. So, assuming the standard locations,
linuxrc will load `boot/<ARCH>/common`, `boot/<ARCH>/root`, and `boot/<ARCH>/bind` in the first case and
`boot/<ARCH>/common` and `boot/<ARCH>/rescue` in the second.

More general the syntax is:

```sh
part:      sub_part1 sub_part2 ...
sub_part1: sub_sub_part11 sub_sub_part12 ...
```

You can modify a part specification by:

1. prefixing it with `?` to mark it optional
2. appending `?list=path_spec1,path_spec2,...` to indicate you need not the
full image but only the files matching any of the path specs (shell glob
syntax)
3. appending `?copy=1` to indicate that this is not an image or archive to mount or unpack but a
plain file to copy to the `/` directory
4. using `<lang>` (verbatim!) as macro to be replaced by the current locale

With this in mind a more realistic config file example could look like:

```sh
root:               common root bind ?cracklib-dict-full.rpm ?yast2-trans-<lang>.rpm ?configfiles
rescue:             common rescue ?cracklib-dict-full.rpm
configfiles:        control.xml?copy=1
yast2-trans-ko.rpm: yast2-trans-ko.rpm un-fonts.rpm?list=*/UnDotum.ttf

```

Here, the inst-sys would consist of `common`, `root`, `bind`, `cracklib-dict-full.rpm` (if it exists),
`yast2-trans-en_US.rpm` (if it exists and assuming current locale is `en_US`), and additionally (if it exists),
`control.xml` is downloaded and stored as `/control.xml`.

For the Korean locale we'll need also a special font rpm (`un-fonts.rpm`) but only `UnDotum.ttf` from it.

### 5.2. files read from repo location

In addition to the files described in sections 1. and 2., linuxrc will try to read these files (and store them in `/`):

- `/media.1/info.txt`
- `/license.tar.gz`
- `/part.info`
- `/control.xml`

and, if **no** `AutoYaST` option has been given, it will read **and parse**

- `/autoinst.xml`

and then add an `AutoYaST` option pointing to the downloaded file.

Note this happens **after** downloading the files in section 3.1. and may overwrite them.

