This is the early part of the SUSE installation process, before
[YaST](https://en.opensuse.org/Portal:YaST) runs.

See <https://en.opensuse.org/SDB:Linuxrc>.

## Debugging

### Run on Installed System
Linuxrc can run on installed system. It runs in testmode that make debugging easier.

### Useful Shortcuts

* ctrl+c then 'q' - exit linuxrc

* ctrl+c then 'c' - change config

* ctrl+c then 'i' - show info detected by linuxrc

* ctrl+c then 's' - start shell

### Logging
To capture log into file with maximum log verbosity use these linuxrc params:
`linuxrc.log=/tmp/linuxrc.log linuxrc.debug=4`
