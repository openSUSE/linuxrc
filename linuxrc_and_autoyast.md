# AutoYaST handling in linuxrc

## Why?

AutoYaST is something very specific to YaST and linuxrc has nothing to do
with it. So why would it have to deal with it at all?

In fact, linuxrc did ignore the `autoyast` boot option for a long time and
just passed it on via `/etc/install.inf` to yast.

There are several reasons why linuxrc has to get involved:

1. You can embed linuxrc options into the AutoYaST config
  https://doc.opensuse.org/projects/autoyast/#invoking_autoinst.linuxrc.

2. An AutoYaST config file `/autoinst.xml` is searched for and auto-loaded from a disk with label `OEMDRV`.

3. There is the expectation that using `autoyast=ftp://foo/bar` (or any network URL)  causes linuxrc
  to implicitly set up the network; much like `install=URL` or`dud=URL` does.

For 3. to work linuxrc has to download the AutoYaST file to be sure the
network setup has been done correctly.

For 2. (and partly 1.) there had been the `autoyast2` option. But that was
limited to linuxrc-style URLs and did not make any attempt to deal with
AutoYaST rules.

The main obstacles so far to get it right were:

- AutoYaST has its own idiosyncratic set of URL schemes
- it's not obvious which file should actually be loaded when AutoYaST rules come into play

## Implementation

1. linuxrc supports all AutoYaST URL schemes (see [References](#references) below); this
implies you can also use AutoYaST-style URLs in other places in linuxrc
2. linuxrc downloads the AutoYaST config file and parses it for linuxrc options - unless it's
a rules-based setup
3. linuxrc converts the URL used with the `autoyast` option into the canonical AutoYaST format and
passes this on via `/etc/install.inf`; this means you can use both linuxrc-style and AutoYaST-style URLs
in linuxrc
4. if the AutoYaST URL ends with a `/` (pointing to a directory) linuxrc
assumes this to be a rules-based setup; for URLs with a mountable scheme,
linuxrc goes looking for the specified directory; for all other URL schemes
linuxrc does nothing and simply passes the URL on to YaST
5. for URL schemes `usb` and `label` linuxrc identifies the device and converts the URL to a `device` scheme
to ensure AutoYaST reads the same config; but if linuxrc could not find the AutoYaST file at
the specified location, the original URL is passed
6. for the `slp` scheme, linuxrc does the URL query and offers the user a
selection dialog; the selected URL is then passed on to YaST; this makes the
`slp` scheme work as documented (with `descr=XXX` query parameter) - which
so far did not work
7. linuxrc implicitly looks for an AutoYaST config file `autoinst.xml` in the repository directory; the
file is downloaded as `/autoinst.xml` and a link to it passed on to YaST (using a `file` URL)
8. there is not really an `autoyast=default` option; if this option is used, it just clears any previous
autoyast setting; the reason is that `autoyast=default` is the default - linuxrc **always** looks for
an AutoYaST config in the repository as described in 7.; see also
https://en.opensuse.org/SDB:Linuxrc#AutoYaST_Profile_Handling
9. as this is a major change and issues are likely to show up, there is a boot option to get back
the old behavior: `autoyast.parse=0` - with this the `autoyast` option is left alone and just passed
on to YaST
10. the `autoyast2` option still exists for compatibility; it is ignored when `autoyast` is used
11. there's a short alias `ay` for the `autoyast` option

## References

AutoYaST URL scheme doc

- https://doc.opensuse.org/projects/autoyast/#Commandline.ay

Understanding AutoYaST rules handling

- https://doc.opensuse.org/projects/autoyast/#handlingrules

Implementation of AutoYaST config file reading in YaST

- https://github.com/yast/yast-installation/blob/master/src/lib/transfer/file_from_url.rb
- https://github.com/yast/yast-autoinstallation/blob/master/src/modules/AutoinstConfig.rb
