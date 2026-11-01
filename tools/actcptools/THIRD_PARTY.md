# ACTCPTools third-party source notice

`ping.c`, `traceroute.c` and `arp.c` are derived from the classic-Amiga ports included with the AmigaOS 3.2 networking development material under `SANA+RoadshowTCP-IP/source_code/4.4BSD-Lite2`.

The supplied README identifies the Amiga ports as work by Olaf Barthel and states that his changes are placed in the public domain.

The underlying 4.4BSD-Lite2 source remains under its original University of California, Berkeley licence. The complete copyright and redistribution conditions are retained at the top of each derived source file.

The original licence includes the historical advertising acknowledgement requirement. Any redistribution of binaries built from these files must preserve the required notices in accompanying documentation/materials.

`hostname.c`, `resolve.c`, `opensocket.c`, `ifconfig.c`, `route.c`, `netstat.c`, `ostool.c`, `host.c`, `whois.c`, `finger.c`, `telnet.c`, `ftp.c`, `tftp.c`, `sntp.c`, `httpget.c` and `nc.c` are original GFOpenSocket (formerly ACNet) sources and use the repository's MIT licence (`LICENSE`, AmiCradle Team).

`ping`, `traceroute` and `arp` above keep their University of California licence inside the MIT project; their notices travel with them.

`httpget` opens `gfopentls.library` (GFOpenTLS, the AmiCradle Team's, MIT) at run time for https://; `third_party/opentls` holds a copy of its API headers (MIT, `third_party/opentls/LICENSE`). Until 19 October 2026 it could be built with AmiSSL 5 instead; nothing of AmiSSL was ever included here.

No AmigaOS NDK headers or libraries are redistributed by ACTCPTools. They are supplied separately to the local build stove.