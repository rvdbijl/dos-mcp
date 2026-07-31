DOS MCP HARDWARE COMMISSIONING BUNDLE
=====================================

Copy this entire directory to C:\DOSMCP on the DOS PC.

Before loading RA-TSR:

1. Load the packet driver for the Ethernet adapter.
2. Edit MTCP.CFG. Set PACKETINT to the packet driver's software interrupt.
3. Set IPADDR, NETMASK, GATEWAY, and NAMESERVER for the local network.
4. Edit STARTTSR.BAT to give this PC a unique visible name if desired.
5. Run STARTTSR with a unique passphrase, for example:

     C:\DOSMCP> STARTTSR Correct-Horse-Battery-Staple

The passphrase is visible in the DOS command line and environment while the
command runs. Do not reuse an important password. The batch file never starts
the endpoint without a credential.

Run RA-TSR with no arguments to query it. Run STOPTSR to unload it. Unload may
refuse if a later TSR owns one of its interrupt vectors or a transfer is active.

RAGENT.EXE is the foreground alternative. It also reads IPADDR and PACKETINT
from MTCPCFG when those command-line positions are omitted or '-':

     SET MTCPCFG=C:\DOSMCP\MTCP.CFG
     RAGENT PASS:Your-Lab-Passphrase - 21300 -

PROTOCHK.EXE and CFGCHK.EXE are offline self-tests. They do not use the network.
Run them after copying the bundle to catch damaged or incompatible executables.

This endpoint has its own small UDP/IP implementation. It shares mTCP's config
file format, but does not link to the mTCP network stack. Some packet drivers do
not permit two programs to claim the same Ethernet packet types simultaneously;
exit foreground mTCP tools before loading RA-TSR if packet initialization fails.

MTCP.CFG uses documentation-only example addresses. It will not work until it
is edited for the target LAN.
