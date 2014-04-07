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

And then multiply it a few times, to take advantage of raid0 striping and raid1
mirroring, depending on your setup. Also, the bigger the sequential IOs, the
faster it will go. And then ignore that number and just use `8M` or `16M`
anyway.

Or for a normal disk, just use `8M`. Technically `8k` is the minimum,
but the smaller the unit size the slower the migration will be (and it gets a
LOT slower). (I think `16M` is the largest data offset supported (16-bit count
of 512-byte sectors), but I could be wrong.)

The only advantage to a smaller block size is you waste that much less space at
the front of the block device. For multi-TB volumes, 4-8MB loss is probably
noise, and 8MB block size will making the migration much much faster than
smaller sizes.

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

Why?
====

`blocks` doesn't support md devices well and this seemed straightforward. Also
my NAS box is on a UPS and the contents aren't too valuable.

Misc
====

My machine crashed during migration! How do I resume it without corruption?

Look at the last line in `bcachify.log` (or, and only if `bcachify.log` doesn't
have any "Copying" lines yet, `bcachify.log.0`):

    Copying 2877208657920 (block: 5487840) to 2877209182208 (block 5487841)

Use the destination offset, 2877209182208 (in bytes, not blocks), to re-start
bcachify. You must use the exact same block size as before:

    $ tail -F bcachify.log &
    $ bcachify /dev/foo BLOCK_SZ_BYTES 2877209182208
    Copying 2877208657920 (block: 5487840) to 2877209182208 (block 5487841)
    ...

Design/Hacking
==============

This is a pretty simple block-at-a-time copier. We copy from tail to head of
the block device, much like the equivalent `memmove(N, 0, M)`.

bcachify.log acts as a sort of write-ahead-log where we synchronously write our
intended copy operation. The last copy in the WAL on crash is idempotent and
safe to replay, and then we resume from there. The block device and
write-ahead log are both used in `O_SYNC` mode. At 2 MB the log is rotated to
bcachify.log.0.

Suggested improvements:

* Default to and/or restrict block size to a larger size (4M? 8M?)
* More validation, sanity checks
    * Validate the filesystem on the blockdevice ends `BLOCK_SZ` before the
      block device does
    * ...
