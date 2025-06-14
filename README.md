# vSwitch
L2 VPN based on a virtual switch. Simulates an Ethernet switch providing frame exchange in a virtual overlay network. The underlay data transmission is based on UDP sockets.
Note that the P in VPN is sort of a lie at this point in time, as the data is not encrypted in trasit with this current implementation.

## System Architecture
```
+------------------------------------------------------------+
|                                                COMPUTER A  |
| +------------------+     +-------------------------------+ |
| | Usermode Program | <=> | Linux Kernel Networking Stack | |
| +------------------+     +------------------------^^-----+ |
|                                                   ||       |
|       +------------+     +-------+     +----------vv---+   |
|  ||=> | UDP Socket | <=> | vPort | <=> | TAP Interface |   |
|  ||   +------------+     +-------+     +---------------+   |
+--||--------------------------------------------------------+
   ||
+--vv--------------+
| Underlay Network <========||
+------------------+        ||
                          +-vv------+
                          | vSwitch |
                          +-^^------+
+------------------+        ||
| Underlay Network <========||
+--^^--------------+
   ||
+--||--------------------------------------------------------+
|  ||                                            COMPUTER B  |   
|  ||   +------------+     +-------+     +---------------+   |
|  ||=> | UDP Socket | <=> | vPort | <=> | TAP Interface |   |
|       +------------+     +-------+     +----------^^---+   |
|                                                   ||       |
| +------------------+     +------------------------vv-----+ |
| | Usermode Program | <=> | Linux Kernel Networking Stack | |
| +------------------+     +-------------------------------+ |
+------------------------------------------------------------+
```

## Packet Structure
The vPort client creates an ethernet-in-ethernet tunnel in which the inner Ethernet frame is encapsulated by a 1-byte custom header used for
control information of the switch (see `include/proto.h`). Overall, the frame structure is as follows:
```
+-----------------------------------------------------------------------------------------------------------+
| Ethernet Header | IP Header | UDP Header | Control Header | Ethernet Header | IP Header | Payload (L4-L7) |
+-----------------------------------------------------------------------------------------------------------+
```

## Features
### CLI tool
The vSwitch can be controlled and monitored using the `vswsh` tool. The tool needs to be installed locally on the same machine
as the switch as the two communicate over a local Unix domain socket. Use the `help` command to get started.

### Trunking
It is possible to create virtual "trunk links" - concatenating two virtual switches together, forwarding frames to one another. It is achieved by
using the `trunk` command. As expected, each switch sees its peer as just another connect client.
Note that trunking should be configured on both ends, otherwise the data would only be able to flow in one direction.

### Statistics & Diagnostics
The switch tracks all kinds of interface statistics which may be of interest to you. You can take a look at them with the `show stats` command.
Other diagnostic commands, e.g *show* commands, are also available in the CLI.

## Installation
Build the project with `make`, provided you're using GCC.
