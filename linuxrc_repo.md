#specifying installation repository in linuxrc

##1. regular install media

*(repo meta data are in /suse/setup/descr/)*

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


##2. plain repomd repository

*(repo meta data are in /repodata/)*

linuxrc identifies this repo by checking for a file '/repodata/repomd.xml' in
the installation repository. This file's signature is not checked (linuxrc
does not parse this file).

As there's normally no installation system included in such a repository,
you'll have to pass its location using the `instsys` option. For example,
the openSUSE Tumbleweed repo has repomd data.

Normally you would use:

```sh
install=http://download.opensuse.org/tumbleweed/repo/oss
```

but you can also use repomd:

```sh
install=http://download.opensuse.org/tumbleweed/repo/oss/suse instsys=../boot/x86_64/root
```

Note that unless you specify also `insecure=1` in the latter case, you'll
get warnings about linuxrc not being able to verify the downloaded images.

Lets see how to avoid this.

###2.1. solving the digest problem

As there's no longer a `content` file, linuxrc needs to get the digests in
some other way. Fortunately it parses `content` just like any other config
file, so you can simply copy it into linuxrc's config directory and add that
to the initrd.

```sh
mkdir -p /tmp/foo/etc/linuxrc.d
cp content /tmp/foo/etc/linuxrc.d/
cd /tmp/foo
find . | cpio -o -H newc | xz --check=crc32 -c >>initrd_on_boot_medium
```

