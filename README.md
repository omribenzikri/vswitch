# vSwitch

L2 VPN based on a virtual switch. Simulates a virtual Ethernet switch providing frame exchange in a virtual overlay network. The underlay data transmission is based on UDP sockets.

```
+------------------------------------------------------------+
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
| Underlay Network =======||
+------------------+      ||
                          +---------+
                          | vSwitch |
                          +---------+
+------------------+      ||
| Underlay Network =======||
+--^^--------------+
   ||
+--||--------------------------------------------------------+
|  ||   +------------+     +-------+     +---------------+   |
|  ||=> | UDP Socket | <=> | vPort | <=> | TAP Interface |   |
|       +------------+     +-------+     +----------^^---+   |
|                                                   ||       |
| +------------------+     +------------------------vv-----+ |
| | Usermode Program | <=> | Linux Kernel Networking Stack | |
| +------------------+     +-------------------------------+ |
+------------------------------------------------------------+
```

The vPort client creates an ethernet-in-ethernet tunnel encapsulated by a 1-byte header used for
control information (see `include/proto.h`). Overall, the frame structure is as follows:

```
+-----------------------------------------------------------------------------------------------------------+
| Ethernet Header | IP Header | UDP Header | Control Header | Ethernet Header | IP Header | Payload (L4-L7) |
+-----------------------------------------------------------------------------------------------------------+
```

The vSwitch can be controlled and monitored using the `vswsh` tool. The tool needs to be installed locally on the same machine
as the switch as the two communicate over local Unix domain sockets.