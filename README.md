# unified-filesystem

A cross-platform tool which combines different physical storage devices and creates a virtual drive.

Supports encryption.

Example: I have 2 hard drives (2TB each) and a flash drive(64GB). I define a virtual drive with this tool
and use my 2 hard drives and a flash drive for base storage. This tool creates a virtual drive (4TB +
64GB) and the operating system mounts it. Now, I can use the file explorer of the operating system and
use the virtual drive like an ordinary drive.


how to build:

make
./unified_filesystem --virtual_drive_path /home/zeroday/Desktop/cse496/unified-filesystem/test --hard_drive /media/zeroday/merve --hard_drive /media/zeroday/volume

note:

virtual drive path:  /home/zeroday/Desktop/cse496/unified-filesystem/test
hard drive paths: /media/zeroday/merve  /media/zeroday/volume

you can give as many as hard drive.
