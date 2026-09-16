# Third-Party Notices

TrafficMonitor Mouse Battery is an independent community project and is not affiliated with or endorsed by Logitech, MCHOSE, or the TrafficMonitor project. Product names and trademarks belong to their respective owners.

## TrafficMonitor

The plugin ABI is implemented against the public TrafficMonitor Plugin API v8.
Reference project: <https://github.com/zhongyang219/TrafficMonitor>

## LGSTrayBattery

The Logitech HID++ discovery/battery behavior was implemented with reference to:
<https://github.com/andyvorld/LGSTrayBattery>

The native path uses Logitech VID 0x046D, vendor-defined HID collections, HID++ 2.0
features 0x1000 / 0x1001 / 0x1004, and the 0x1001 millivolt-to-percentage
lookup data published by LGSTrayBattery. LGSTrayBattery is GPL-3.0 licensed.
Accordingly, this project is distributed under GPL-3.0-or-later.

## dsh-mchose-battery

The MCHOSE HID query/decode behavior was implemented with reference to:
<https://github.com/Fransice/dsh-mchose-battery>

Referenced public protocol behavior includes MCHOSE VID 0x3837, config UsagePage
0xFF01, report IDs 0x11/0x13, the profile-reload payload, XOR-0xFF E2 status
decoding, battery/charging/name byte offsets, and wired/wireless PID preference.
dsh-mchose-battery is MIT licensed.

MCHOSE G3 A support was developed through interoperability testing with a user-owned device and the behavior exposed by MCHOSE's public web configuration application. No vendor source code, firmware, or captured device identifiers are distributed by this project.
