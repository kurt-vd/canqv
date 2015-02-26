## canqv

canqv is a small C-program which shows a (subset of a) list of
CAN frames found on a CAN bus.
The main goal is to provide a __quick view on CAN__.

## operation

	$ canqv can0

Outputs a _refreshing_ list of found CAN identifiers on the terminal.
The list is sorted, and contains the databytes of the last CAN frame
with the CAN identifier, and a _guess_ of the repetition period is
shown next to the CAN frame.

## canqv vs. cansniffer

cansniffer requires CAN\_BCM sockets, and is limited to 11bit CAN identifiers.

canqv overcomes these limitations, with higher cpu-load as a consequence.

## cross-compile

Cross-compiling is done by creating **config.mk** in the libenumif root.
config.mk may add, overrule, extend Makefile variables.

