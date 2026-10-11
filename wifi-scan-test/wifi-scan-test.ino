/*
 * WiFi Scan & Antenna Diagnostics Test
 *
 * This sketch exercises the ESP32-S3 receive path, to judge whether the antenna
 * is connected and tuned, then the transmit path. It reads and displays:
 * - Chip model, revision, core count
 * - STA and AP MAC addresses (all-zero or all-FF means bad eFuse)
 * - Configured max TX power and regulatory country/channel range
 * - Every AP in range, sorted strongest first, with RSSI / channel / encryption
 * - Summary stats: AP count, strongest RSSI, median RSSI
 *
 * Then it raises an open soft-AP to exercise the TRANSMIT path, and reports its
 * channel, mode, client count and TX power every 5 seconds.
 *
 * Serial commands:
 * - '~' reboots the chip, so the boot log can be captured from the first line
 * - 's' forces another scan (briefly borrows the STA interface back)
 *
 * HOW TO READ THE RESULT
 * A working antenna is NOT proven by "it found networks". Receive tolerates far
 * more loss than transmit needs for a usable link, so a detuned or disconnected
 * antenna still finds strong nearby APs while failing to transmit at all.
 * Antenna loss is reciprocal, so compare the RSSI figures below against what a
 * phone or laptop in the same spot reports for the SAME networks:
 * - within ~5-10 dB   -> antenna is fine, look elsewhere (supply, PA)
 * - 20-30 dB or worse -> antenna is the fault
 *
 * Arduino IDE settings for the ESP32-S3 SuperMini:
 * - Board: "ESP32S3 Dev Module"
 * - USB CDC On Boot: "Enabled"   (otherwise there is no serial output)
 * - PSRAM: "QSPI PSRAM"
 */

#include <WiFi.h>
#include <esp_wifi.h>

// (scans are now boot-once plus the 's' command; the AP owns the radio after that)
#define SERIAL_BAUD      115200

// TX test: raise an open soft-AP after the first scan. Channel 1 is used because
// the scan proves the radio receives well there, so if TX works at all it should
// work on 1. Try 6 as well; WLED's AP was on 6 and never appeared.
#define AP_TEST_SSID     "TXTEST-AP"
#define AP_TEST_CHANNEL  1
#define STATUS_INTERVAL_MS 5000

// Include hidden networks in the scan results
#define SCAN_SHOW_HIDDEN true

static void printMac(const char *label, wifi_interface_t iface) {
  uint8_t mac[6] = {0};
  esp_wifi_get_mac(iface, mac);
  Serial.printf("%-10s %02X:%02X:%02X:%02X:%02X:%02X\n", label,
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

static const char *encName(wifi_auth_mode_t enc) {
  switch (enc) {
    case WIFI_AUTH_OPEN:            return "open";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-ent";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    default:                        return "?";
  }
}

static void printRadioInfo() {
  Serial.println(F("=== Radio Info ==="));
  Serial.printf("Chip:      %s rev %d, %d core(s)\n",
                ESP.getChipModel(), ESP.getChipRevision(), ESP.getChipCores());

  printMac("STA MAC:", WIFI_IF_STA);
  printMac("AP MAC:", WIFI_IF_AP);

  int8_t power = 0;
  if (esp_wifi_get_max_tx_power(&power) == ESP_OK) {
    Serial.printf("TX power:  %d (%.2f dBm)\n", power, power / 4.0);
  }

  wifi_country_t c;
  if (esp_wifi_get_country(&c) == ESP_OK) {
    Serial.printf("Country:   %c%c, channels %d-%d, max %d dBm\n",
                  c.cc[0], c.cc[1], c.schan, c.schan + c.nchan - 1,
                  c.max_tx_power);
  }
  Serial.println();
}

// Sort indices by RSSI, strongest first (insertion sort; n is small)
static void sortByRssi(int *idx, int n) {
  for (int i = 1; i < n; i++) {
    int key = idx[i];
    int j = i - 1;
    while (j >= 0 && WiFi.RSSI(idx[j]) < WiFi.RSSI(key)) {
      idx[j + 1] = idx[j];
      j--;
    }
    idx[j + 1] = key;
  }
}

static void runScan() {
  Serial.println(F("--- Scanning ---"));
  unsigned long t0 = millis();
  int n = WiFi.scanNetworks(false, SCAN_SHOW_HIDDEN);  // blocking
  unsigned long elapsed = millis() - t0;

  if (n < 0) {
    Serial.printf("Scan failed (%d) after %lums -- RX path is suspect\n", n, elapsed);
    return;
  }
  if (n == 0) {
    Serial.printf("Scan found 0 networks after %lums -- RX path is suspect\n", elapsed);
    WiFi.scanDelete();
    return;
  }

  int *idx = (int *)malloc(sizeof(int) * n);
  if (!idx) {
    Serial.println(F("malloc failed"));
    WiFi.scanDelete();
    return;
  }
  for (int i = 0; i < n; i++) idx[i] = i;
  sortByRssi(idx, n);

  Serial.printf("Found %d networks in %lums:\n", n, elapsed);
  Serial.println(F("  RSSI  Ch  Enc       SSID"));
  for (int i = 0; i < n; i++) {
    int k = idx[i];
    String ssid = WiFi.SSID(k);
    Serial.printf("  %4d  %2d  %-8s  %s\n",
                  WiFi.RSSI(k), WiFi.channel(k), encName(WiFi.encryptionType(k)),
                  ssid.length() ? ssid.c_str() : "<hidden>");
  }

  int strongest = WiFi.RSSI(idx[0]);
  int median    = WiFi.RSSI(idx[n / 2]);
  Serial.printf("\nStrongest: %d dBm   Median: %d dBm   Count: %d\n",
                strongest, median, n);
  Serial.println(F("Compare 'Strongest' with what a phone/laptop sees here for"
                   " the same AP. A 20+ dB deficit means the antenna is at fault."));

  free(idx);
  WiFi.scanDelete();
}

void setup() {
  Serial.begin(SERIAL_BAUD);

  // On native USB CDC, output before the host opens the port is lost. Wait for
  // the connection, but give up so the sketch still runs headless. For UART0,
  // operator bool() is always true, so this falls through immediately.
  unsigned long t0 = millis();
  while (!Serial && millis() - t0 < 5000) delay(50);
  delay(500);

  Serial.println(F("\n\n=== WiFi Scan & Antenna Diagnostics ==="));

  // persistent(false) means no stored credentials are applied, so the radio
  // comes up idle. Do NOT call WiFi.disconnect(true) here: wifioff=true runs
  // esp_wifi_stop(), which races the netif rx callback registration and yields
  // "netstack cb reg failed with 12308" (ESP_ERR_WIFI_STOP_STATE).
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  delay(200);

  printRadioInfo();
}

static bool apRunning = false;

static void startTestAp() {
  Serial.println(F("--- Starting open soft-AP (TX test) ---"));
  // AP-only, not AP_STA: gives the radio entirely to the beacon. WLED runs
  // AP_STA, so if this works and WLED's AP does not, that difference matters.
  WiFi.mode(WIFI_AP);
  delay(100);
  bool ok = WiFi.softAP(AP_TEST_SSID, nullptr, AP_TEST_CHANNEL);
  Serial.printf("softAP(\"%s\", open, ch %d) -> %d\n",
                AP_TEST_SSID, AP_TEST_CHANNEL, (int)ok);
  Serial.print(F("AP IP:   "));
  Serial.println(WiFi.softAPIP());
  printMac("AP MAC:", WIFI_IF_AP);
  Serial.printf("\nScan for \"%s\" from a phone or laptop.\n", AP_TEST_SSID);
  Serial.println(F("Invisible here means the transmit path is at fault: this"
                   " sketch has no W5500, no LEDs and none of WLED's code."));
  Serial.println();
  apRunning = ok;
}

// '~' reboots, 's' forces a rescan. The CDC wait at the top of setup() means the
// boot log is captured from the first line after a '~'.
static void handleSerialCommands() {
  while (Serial.available() > 0) {
    char c = Serial.read();
    if (c == '~') {
      Serial.println(F("\n*** Restarting ***"));
      Serial.flush();
      delay(50);
      ESP.restart();
    } else if (c == 's') {
      // Scanning needs the STA interface, so borrow it and hand the radio back
      if (apRunning) { WiFi.mode(WIFI_AP_STA); delay(100); }
      runScan();
      if (apRunning) { WiFi.mode(WIFI_AP); delay(100); }
      Serial.println();
    }
  }
}

void loop() {
  static unsigned long lastScan   = 0;
  static unsigned long lastStatus = 0;

  handleSerialCommands();

  // One scan first (RX check), then hand the radio to the AP (TX check). Running
  // scans alongside the AP would retune the radio and disrupt the beacon.
  if (lastScan == 0) {
    lastScan = millis();
    runScan();
    Serial.println();
    startTestAp();
    return;
  }

  if (apRunning && millis() - lastStatus >= STATUS_INTERVAL_MS) {
    lastStatus = millis();

    uint8_t primary = 0;
    wifi_second_chan_t second;
    esp_wifi_get_channel(&primary, &second);

    int8_t power = 0;
    esp_wifi_get_max_tx_power(&power);

    Serial.printf("[%6lus] mode=%d ch=%u clients=%u txpower=%d (%.2f dBm)\n",
                  millis() / 1000, (int)WiFi.getMode(), (unsigned)primary,
                  (unsigned)WiFi.softAPgetStationNum(), power, power / 4.0);
  }
}
