LINUX=/kernel/src/linux
PREFIX=
MODDIR=/lib/modules/2.2.13
PCDEBUG=
KFLAGS=
UFLAGS=
# UNSAFE_TOOLS is not defined
CONFIG_CARDBUS=y
# CONFIG_PNP_BIOS is not defined

# Options from /kernel/src/linux/.config
# CONFIG_PCMCIA is not defined
# CONFIG_SMP is not defined
CONFIG_PCI=y
CONFIG_PCI_QUIRKS=y
# CONFIG_APM is not defined
CONFIG_SCSI=y
CONFIG_INET=y
CONFIG_NET_RADIO=y
CONFIG_TR=y
# CONFIG_MODVERSIONS is not defined
CONFIG_PROC_FS=y
CONFIG_1GB=y
# CONFIG_2GB is not defined
# CONFIG_3GB is not defined
ARCH=i386
AFLAGS=
CONFIG_ISA=y

MFLAG=-DMODVERSIONS -include ../include/linux/modversions.h
UTS_RELEASE=2.2.13
UTS_VERSION=#5 Sun Oct 31 14:35:11 CET 1999
LINUX_VERSION_CODE=131597

NEW_QLOGIC=y
HAS_PROC_BUS=y
DO_IDE=y
DO_PARPORT=y
# FIX_AHA152X is not defined
DO_APA1480=y
# FIX_AIC7XXX is not defined
DO_EPIC_CB=y
RC_DIR=/etc/rc.d
SYSV_INIT=y
# INSTALL_DEPMOD is not defined
# HAS_FORMS is not defined
