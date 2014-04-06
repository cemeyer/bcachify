Bcachify
========

A stupid stupid program to migrate existing block devices sideways to make room
for the bcache superblock in the most dangerous way possible. But at least it's
trivially understandable.

Usage
=====

First: don't.

Say you have an ext4 filesystem on `/dev/mdXXX`. First, discover the chunk
size:

    $ cat /proc/mdstat | grep chunks
    ... 512K chunks ...

Or for a normal disk, just use `256k` to `1M`. Technically `8k` is the minimum,
but the smaller the unit size the slower the migration will be.

Next, use `resize2fs` to shrink the filesystem down to leave at least the chunk
size free at the end of the block device.

Then:

    $ tail -F bcachify.log &
    $ bcachify /dev/mdXXX $((512*1024)) &
    ================= Starting ==================
    Dev: /dev/md127 SB_SPACE: 524288
    Copying 4000791396352 (block: 7630904) to 4000791920640 (block 7630905)
    ...

Wait several hours, and then:

    Okay, now invoke make-bcache like this:
    make-bcache --bdev --data_offset 1024 --block XXX [--cset-uuid UUID]
    (data_offset is in units of 512-byte sectors; the argument
    to --block is in bytes but must be a multiple of 512-byte sectors
    and a power of two.)

Note: logs will get very big, you probably want to make bcachify.log a fifo and
gzip it.

Why?
====

`blocks` doesn't support md devices well and this seemed straightforward. Also
my NAS box is on a UPS and the contents aren't too valuable.
