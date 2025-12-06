## Status

- Working! with specific configuration (eg no Wifi because it steamrolls the ETH)

## Setup

- No additional components, just hardwire the W5500 module into the hardcoded pins in network.cpp
- Do not connect Wifi since Wifi and ETH share a static IP address which will never work
- Wifi appears to have priority over ETH
- Connect to a switch (no internet, no DHCP)
  - `ETH-E: Connected` message appears in debug mode
- Connect to the WLED-AP access point and set a static IP address
  - 169.254.x.x is the Automatic Private IP Addressing (APIPA) space
  - 169.254.120.123 is what I used
  - Gateway 169.254.1.1
  - Subnet 255.255.0.0
- Connect laptop to the switch too
  - Shows up in network settings as Self-assigned IP (169.254.129.160 in this case)
- Visit WLED address 169.254.120.123 in a browser, WLED page loads through the switch

## Things to do

- Document decisions (like why we needed to use non-Tasmota platform)
  - ETHClass2 calls w5500 functions that are not compiled into Tasmota version
  - ESP-IDF minimum 4.4ish for those functions

## Recent Changes

### 2025-12-05: Separate Ethernet and WiFi Static IP Configuration

Added support for separate static IP configuration for Ethernet and WiFi interfaces. Previously both interfaces shared the same static IP settings (from multiWiFi[0]), which caused conflicts when both were enabled.

**Changes made:**
1. **wled.h:398-400** - Added three new global variables:
   - `ethernetStaticIP` - Ethernet static IP address
   - `ethernetStaticGW` - Ethernet static gateway
   - `ethernetStaticSN` - Ethernet static subnet mask

2. **network.cpp:453-454** - Updated Ethernet initialization to use Ethernet-specific IP config instead of WiFi config

3. **cfg.cpp:55-64** - Added deserialization (loading) of Ethernet static IP settings from JSON config

4. **cfg.cpp:924-931** - Added serialization (saving) of Ethernet static IP settings to JSON config

5. **set.cpp:113-120** - Added handling of Ethernet IP settings from web interface form (EI0-3, EG0-3, ES0-3)

6. **xml.cpp:251-262** - Added Ethernet static IP values to settings JavaScript

7. **settings_wifi.htm:275-281** - Added HTML input fields for Ethernet static IP configuration in web interface

**Usage:**
- Navigate to WiFi Settings page
- Scroll to "Ethernet Type" section
- Configure Ethernet static IP, gateway, and subnet separately from WiFi
- Leave at 0.0.0.0 for DHCP
- Both WiFi and Ethernet can now coexist with different static IPs
- IP changes apply instantly; device only reboots if Ethernet type changes

### 2025-12-05: Automatic Link-Local IP Assignment (APIPA)

Added automatic link-local IP (169.254.x.x) assignment when DHCP is unavailable, with automatic upgrade to DHCP when it becomes available.

**Changes made:**
1. **wled.h:401-402** - Added tracking variables
   - `ethernetDhcpStartTime` - Timestamp when DHCP was requested
   - `ethernetLinkLocalAssigned` - Flag to track link-local state

2. **network.cpp:460-465** - Track DHCP start time in ETH_CONNECTED event
   - When using DHCP, record timestamp and prepare for link-local fallback

3. **network.cpp:475-477** - Reset state on ETH_DISCONNECTED

4. **wled.cpp:882-930** - APIPA implementation with automatic DHCP upgrade
   - **Fast fallback:** 3-second timeout (not 15!) for quick link-local assignment
   - **Periodic DHCP retry:** Every 30 seconds, attempts DHCP again
   - **Automatic upgrade:** Detects when DHCP becomes available and switches automatically
   - Uses 169.254.1.0 - 169.254.254.255 range (avoiding .0 and .255)
   - Gateway set to 169.254.1.1, subnet 255.255.0.0

5. **set.cpp:145-154** - Reset state when IP config changes

**Behavior:**
- **DHCP available at boot:** Uses DHCP-assigned IP immediately
- **DHCP unavailable at boot:** After 3 seconds, auto-assigns 169.254.x.x
- **DHCP becomes available later:** Automatically switches from link-local to DHCP (checked every 30s)
- **Manual static IP:** No DHCP attempt, no link-local fallback
- **Random IP:** Each boot gets different 169.254.x.x to avoid conflicts

**Timeline example (no DHCP):**
```
0s:  Ethernet connected, requesting DHCP
3s:  No DHCP response → assign 169.254.147.89
33s: Retry DHCP (still unavailable, keep link-local)
63s: Retry DHCP (still unavailable, keep link-local)
... continues every 30 seconds
```

**Timeline example (DHCP added later):**
```
0s:   Ethernet connected, requesting DHCP
3s:   No DHCP response → assign 169.254.147.89
33s:  Retry DHCP → DHCP now available! → switch to 192.168.1.100
      Device reconnects, web interface available at new IP
```

**Why APIPA with auto-upgrade:**
- **Fast:** 3-second fallback means quick connectivity
- **Automatic:** No manual intervention when DHCP added to network
- **Seamless:** Upgrades to DHCP without reboot
- **Standard:** RFC 3927 compliant APIPA implementation
- **Reliable:** Works with or without DHCP server

### 2025-12-05: Smart Ethernet Reconfiguration

Added smart handling of Ethernet configuration changes - reconfigures IP without reboot when possible, only reboots when necessary.

**Changes made:**
1. **set.cpp:112-146** - Intelligent Ethernet configuration change detection
   - Stores old values before reading new settings
   - Detects if Ethernet type changed vs. just IP settings changed
   - **Ethernet type changed:** Triggers full reboot (ETH.begin() needs to be called with new hardware)
   - **Only IP changed:** Calls `ETH.config()` directly to reconfigure without reboot
   - Provides instant IP changes without disrupting service

**Behavior:**
- **Change Ethernet type** (e.g., None → W5500): Device reboots
- **Change static IP only**: IP reconfigured immediately, no reboot needed
- **Switch DHCP ↔ static**: IP reconfigured immediately, no reboot needed

**Why this approach:**
- Ethernet hardware initialization requires `ETH.begin()` which can only run once (static guard flag)
- IP configuration can be changed at runtime via `ETH.config()` without reinitializing hardware
- Minimizes service disruption - most common case (IP changes) happens instantly

### 2025-12-06: Ultra-Fast Link-Local with Exponential Backoff DHCP

Optimized link-local IP assignment and DHCP checking for near-instant connectivity with streaming-friendly DHCP retries.

**Changes made:**
1. **wled.cpp:886** - Reduced link-local timeout from 3 seconds to **250ms**
   - Nearly instant fallback for immediate network connectivity
   - Critical for streaming applications that can't wait 3 seconds

2. **wled.cpp:917-949** - Exponential backoff for DHCP retries
   - **First retry:** 500ms (super fast if DHCP server responds quickly)
   - **Second retry:** 1 second
   - **Third retry:** 2 seconds
   - **Fourth retry:** 4 seconds
   - **Ongoing retries:** 12 seconds
   - Aggressive initial attempts, backs off to non-intrusive periodic checks

3. **network.cpp:458** - Fixed static IP check for link-local IPs
   - Now only requires IP to be non-zero (gateway can be 0.0.0.0)
   - Properly supports link-local static IPs without gateway

4. **set.cpp:140** - Same fix for settings page IP reconfiguration

**Why these timings:**
- **250ms link-local:** Network ready almost instantly, perfect for streaming
- **Exponential backoff:** If DHCP exists, get it quickly (within 7.5s total). If not, settle into non-disruptive 12s checks
- **No packet loss:** All operations are non-blocking, streaming packets continue uninterrupted
- **Gateway optional:** Link-local IPs don't route, so gateway = 0.0.0.0 is valid

**Timeline example (no DHCP, streaming use case):**
```
0ms:    Ethernet link up, requesting DHCP
250ms:  No DHCP response → assign 169.254.x.x (READY FOR STREAMING!)
750ms:  Retry DHCP (attempt 1)
1750ms: Retry DHCP (attempt 2)
3750ms: Retry DHCP (attempt 3)
7750ms: Retry DHCP (attempt 4)
19.75s: Retry DHCP (ongoing, every 12s)
```

### 2025-12-06: Fixed Network Detection for W5500 Static IPs

The W5500 ETHClass2 library doesn't properly report static IP via `ETH.localIP()` - it returns 0.0.0.0 even when the IP is configured and working. This caused incorrect status reporting.

**Changes made:**
1. **Network.cpp:79-100** - Enhanced `isEthernet()` detection
   - Checks `ETH.linkUp()` first (physical connection)
   - Falls back to checking if static IP is configured when `ETH.localIP()` returns 0.0.0.0
   - Uses `ethernetDhcpStartTime == 0` as indicator of static IP mode
   - Returns true if link up AND (ETH.localIP() works OR static IP configured)

2. **Network.cpp:3-28** - Enhanced `localIP()` with fallback
   - If `ETH.localIP()` returns 0.0.0.0 but link is up and static IP configured
   - Returns the configured `ethernetStaticIP` value instead
   - Ensures UI and API show correct IP even when driver doesn't report it

3. **Network.cpp:30-51** - Enhanced `subnetMask()` with fallback
   - Returns configured `ethernetStaticSN` when `ETH.subnetMask()` fails

4. **Network.cpp:53-74** - Enhanced `gatewayIP()` with fallback
   - Returns configured `ethernetStaticGW` when `ETH.gatewayIP()` fails

5. **wled.cpp:289-306** - Improved debug output
   - Shows `ETH.linkUp()` status separately
   - Shows both `ETH.localIP()` and `ethernetStaticIP` for comparison
   - Uses link status for "Connected" message (not IP check)

**Why this fix was needed:**
- W5500 driver has quirks where `ETH.localIP()` may not reflect configured static IP
- Physical connection (link) is more reliable indicator than IP query
- Fallback to configured values ensures system works correctly
- Critical for UI to display correct IP and status

### 2025-12-06: Link-Local IP Trusted Subnet

Fixed "Access Denied" errors when accessing WLED from link-local IP addresses without a PIN configured.

**Changes made:**
1. **wled_server.cpp:61** - Added link-local range to trusted subnets
   - Added `169.254.0.0/16` (APIPA/link-local range) to `inLocalSubnet()` check
   - Joins existing trusted ranges: 10.x.x.x, 192.168.x.x, 172.16.x.x
   - Prevents PIN requirement for local link-local connections

**Why this was needed:**
- Link-local IPs (169.254.x.x) are self-assigned for direct local communication
- Missing from trusted subnet list caused access control to require PIN
- RFC 3927 defines these as local-use only, making them safe to trust
- Consistent with treating other private IP ranges as trusted

**Before fix:**
```
Access from 169.254.x.x → "Access denied" (even without PIN set)
```

**After fix:**
```
Access from 169.254.x.x → Full access (same as 192.168.x.x)
```

## Intermittent

Upon reconnecting the circuit, computer is unable to access 169.254.120.123
For some reason it was unable to route packets to that address
Changing to .124 appears to work
The laptop was also given a static IP instead of trying DHCP
Appears that the ESP32 needs to be unplugged and replugged when this happens
