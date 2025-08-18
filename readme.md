# ED_wifi - wifi library for ESP devices IoT

## Context
- home availability of multiople 4G networks (multiple bridges/extenders AP heading to the same router)
- DNS on a raspberry machine, which is working as web server/data/server/mqtt server/DNS master for the router
-
## Principles
The library is aimed at implementing the following capabilities:
-   scan of available network as ESP32 STA
-   connection the the network with strongest signal
-   in case of failure to connect,
    -   if another known AP is available, try switching to it
    -   if no other available, switch to ESP AP (unprotected? to be decided whether password protect.) to allow connection from devices to service the unit without physical access
-   periodic retry to switch back to the STA mode, after a rescan of the available AP
- todo
  - periodic scanning of available networks to switch to one with stronger signal (management of signal instability)
## not implemented (yet)
-   the interface via AP to troubleshoot and change parameters to fix the device connection
-   chosen not to implement, for the time being, the AP+STA which would be interesting for bridging signal. Unclear how much it can help or create packet conflicts./ maybe on a case by case?