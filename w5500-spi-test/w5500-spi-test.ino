/*
 * W5500 SPI Communication & Diagnostics Test
 *
 * This sketch performs comprehensive diagnostics on a W5500 Ethernet module
 * by reading and displaying:
 * - Chip version (should be 0x04)
 * - MAC address
 * - IP configuration (IP, subnet, gateway)
 * - PHY status (link, speed, duplex)
 * - Control registers (mode, interrupts)
 *
 * Displays a detailed formatted status report every 5 seconds.
 *
 * Pin configuration for ESP32-S3 (from WLED project):
 * - MISO: GPIO 13
 * - MOSI: GPIO 11
 * - SCK:  GPIO 12
 * - CS:   GPIO 10
 * - INT:  GPIO 4  (not used in this test)
 * - RST:  GPIO 5
 */

#include <SPI.h>
#include <stdarg.h>
#include <string.h>
#include <strings.h>

// W5500 pin definitions (ESP32-S3)
#define ETH_MISO_PIN 13
#define ETH_MOSI_PIN 11
#define ETH_SCLK_PIN 12
#define ETH_CS_PIN   10
#define ETH_INT_PIN  9
#define ETH_RST_PIN  8

// W5500 registers and commands
#define W5500_COMMON_REG  0x00  // Common register block
#define W5500_READ        0x00  // Read operation
#define W5500_WRITE       0x04  // Write operation

// W5500 register addresses (Common Register Block)
#define W5500_MR          0x0000  // Mode Register
#define W5500_GAR         0x0001  // Gateway Address Register (4 bytes)
#define W5500_SUBR        0x0005  // Subnet Mask Register (4 bytes)
#define W5500_SHAR        0x0009  // Source Hardware Address Register (6 bytes - MAC)
#define W5500_SIPR        0x000F  // Source IP Address Register (4 bytes)
#define W5500_INTLEVEL    0x0013  // Interrupt Low Level Timer Register (2 bytes)
#define W5500_IR          0x0015  // Interrupt Register
#define W5500_IMR         0x0016  // Interrupt Mask Register
#define W5500_SIR         0x0017  // Socket Interrupt Register
#define W5500_SIMR        0x0018  // Socket Interrupt Mask Register
#define W5500_RTR         0x0019  // Retry Time Register (2 bytes)
#define W5500_RCR         0x001B  // Retry Count Register
#define W5500_PHYCFGR     0x002E  // PHY Configuration Register
#define W5500_VERSIONR    0x0039  // Version Register (should read 0x04)

// IR (Interrupt Register) bit definitions
#define IR_CONFLICT       0x80  // Bit 7: IP conflict interrupt
#define IR_UNREACH        0x40  // Bit 6: Destination unreachable interrupt
#define IR_PPPoE          0x20  // Bit 5: PPPoE connection close interrupt
#define IR_MP             0x10  // Bit 4: Magic packet interrupt

// Socket n Interrupt Register (Sn_IR) bit definitions
#define Sn_IR_CON         0x01  // Bit 0: Connection established
#define Sn_IR_DISCON      0x02  // Bit 1: Disconnection occurred
#define Sn_IR_RECV        0x04  // Bit 2: Data received
#define Sn_IR_TIMEOUT     0x08  // Bit 3: Timeout occurred
#define Sn_IR_SENDOK      0x10  // Bit 4: Send completed

// Socket register base addresses (Socket 0)
#define W5500_S0_BASE     0x0001  // Socket 0 register block
#define W5500_Sn_MR       0x0000  // Socket Mode Register (offset within socket block)
#define W5500_Sn_CR       0x0001  // Socket Command Register (offset within socket block)
#define W5500_Sn_IR       0x0002  // Socket Interrupt Register (offset within socket block)
#define W5500_Sn_SR       0x0003  // Socket Status Register (offset within socket block)
#define W5500_Sn_PORT     0x0004  // Socket Source Port Register (2 bytes, offset within socket block)
#define W5500_Sn_RXBUF_SZ 0x001E  // Socket RX Buffer Size Register (KB)
#define W5500_Sn_TXBUF_SZ 0x001F  // Socket TX Buffer Size Register (KB)
#define W5500_Sn_TX_FSR   0x0020  // Socket TX Free Size Register (2 bytes)
#define W5500_Sn_TX_RD    0x0022  // Socket TX Read Pointer (2 bytes)
#define W5500_Sn_TX_WR    0x0024  // Socket TX Write Pointer (2 bytes)
#define W5500_Sn_RX_RSR   0x0026  // Socket RX Received Size Register (2 bytes)
#define W5500_Sn_RX_RD    0x0028  // Socket RX Read Pointer (2 bytes)
#define W5500_Sn_RX_WR    0x002A  // Socket RX Write Pointer (2 bytes)
#define W5500_Sn_IMR      0x002C  // Socket Interrupt Mask Register (offset within socket block)

// Block select values for socket n: registers, TX buffer, RX buffer
#define BLOCK_SOCK_REG(n) ((n) * 4 + 1)
#define BLOCK_SOCK_TX(n)  ((n) * 4 + 2)
#define BLOCK_SOCK_RX(n)  ((n) * 4 + 3)

// Socket commands
#define Sn_CR_OPEN        0x01
#define Sn_CR_LISTEN      0x02
#define Sn_CR_DISCON      0x08
#define Sn_CR_CLOSE       0x10
#define Sn_CR_SEND        0x20
#define Sn_CR_RECV        0x40

// Socket modes
#define Sn_MR_TCP         0x01

// Socket status values
#define SOCK_CLOSED       0x00
#define SOCK_INIT         0x13
#define SOCK_LISTEN       0x14
#define SOCK_SYNSENT      0x15
#define SOCK_SYNRECV      0x16
#define SOCK_ESTABLISHED  0x17
#define SOCK_FIN_WAIT     0x18
#define SOCK_CLOSING      0x1A
#define SOCK_TIME_WAIT    0x1B
#define SOCK_CLOSE_WAIT   0x1C
#define SOCK_LAST_ACK     0x1D

// Serial status report geometry: characters between the box borders
#define BOX_W             67

// TCP server behaviour
#define TCP_PORT          80
#define SOCK_IO_TIMEOUT   1000  // ms to wait on a send before giving up
#define CMD_BUF_LEN       96    // longest accepted command line

// Interrupt tracking
volatile bool interrupt_occurred = false;
volatile unsigned long interrupt_count = 0;
volatile unsigned long last_interrupt_time = 0;

// Link state tracking
bool last_link_state = false;

// TCP server state
bool net_configured = false;      // true once setLinkLocalIP() has run
bool client_connected = false;    // true while socket 0 is ESTABLISHED
bool want_close = false;          // set by the 'quit' command
unsigned long conn_count = 0;     // connections accepted since boot
unsigned long rx_bytes = 0;       // bytes received on the current connection
char cmd_buf[CMD_BUF_LEN];        // command line being assembled
uint16_t cmd_len = 0;
uint8_t iac_state = 0;            // telnet IAC escape parser state
unsigned long last_arm_attempt = 0;  // throttles re-LISTEN retries

// PHYCFGR bit definitions
#define PHYCFGR_RST       (1<<7)  // PHY Reset
#define PHYCFGR_OPMD      (1<<6)  // Configure PHY Operation Mode
#define PHYCFGR_OPMDC     (7<<3)  // Operation Mode Configuration
#define PHYCFGR_DPX       (1<<2)  // Duplex Status (0=Half, 1=Full)
#define PHYCFGR_SPD       (1<<1)  // Speed Status (0=10Mbps, 1=100Mbps)
#define PHYCFGR_LNK       (1<<0)  // Link Status (0=Down, 1=Up)

// Function declarations
void IRAM_ATTR w5500_isr();
uint8_t w5500_read_byte(uint16_t addr);
void w5500_read_bytes(uint16_t addr, uint8_t* buf, uint16_t len);
void w5500_write_byte(uint16_t addr, uint8_t data);
void w5500_write_bytes(uint16_t addr, uint8_t* buf, uint16_t len);
uint8_t w5500_read_socket_byte(uint8_t socket_num, uint16_t addr);
void w5500_write_socket_byte(uint8_t socket_num, uint16_t addr, uint8_t data);
void setLinkLocalIP();
void openTCPSocket(bool verbose = true);
void w5500_read_block(uint8_t block, uint16_t addr, uint8_t* buf, uint16_t len);
void w5500_write_block(uint8_t block, uint16_t addr, const uint8_t* buf, uint16_t len);
uint16_t w5500_read_socket_word(uint8_t socket_num, uint16_t addr);
void w5500_write_socket_word(uint8_t socket_num, uint16_t addr, uint16_t value);
uint16_t w5500_rx_available(uint8_t socket_num);
uint16_t w5500_tx_free(uint8_t socket_num);
uint16_t w5500_recv(uint8_t socket_num, uint8_t* buf, uint16_t maxlen);
bool w5500_send(uint8_t socket_num, const uint8_t* data, uint16_t len);
void serviceSocket();
void closeConnection(const char* reason);
void drainRx();
void feedByte(uint8_t c);
void handleLine(char* line);
void sendStr(const char* str);
void sendFmt(const char* fmt, ...);
void sendPrompt();
const char* sockStateName(uint8_t status);
int utf8Width(const char* str);
void boxRow(const char* fmt, ...);
void boxTop(const char* title);
void boxBottom();
void boxBanner(const char* title);

void setup() {
  Serial.begin(115200);
  delay(1000);

  Serial.println("\n\n=== W5500 SPI Test ===");
  Serial.println("Initializing...\n");

  // Initialize reset pin
  pinMode(ETH_RST_PIN, OUTPUT);
  digitalWrite(ETH_RST_PIN, LOW);
  delay(1);
  digitalWrite(ETH_RST_PIN, HIGH);
  delay(200);  // Wait for chip to come out of reset

  // Initialize SPI with custom pins
  SPI.begin(ETH_SCLK_PIN, ETH_MISO_PIN, ETH_MOSI_PIN, ETH_CS_PIN);
  SPI.setFrequency(4000000);  // 4 MHz - safe speed for testing
  SPI.setBitOrder(MSBFIRST);
  SPI.setDataMode(SPI_MODE0);

  // Initialize CS pin
  pinMode(ETH_CS_PIN, OUTPUT);
  digitalWrite(ETH_CS_PIN, HIGH);

  // Initialize interrupt pin as input and attach ISR
  pinMode(ETH_INT_PIN, INPUT_PULLUP);
  attachInterrupt(digitalPinToInterrupt(ETH_INT_PIN), w5500_isr, FALLING);

  Serial.println("Pin Configuration:");
  Serial.printf("  MISO: GPIO %d\n", ETH_MISO_PIN);
  Serial.printf("  MOSI: GPIO %d\n", ETH_MOSI_PIN);
  Serial.printf("  SCK:  GPIO %d\n", ETH_SCLK_PIN);
  Serial.printf("  CS:   GPIO %d\n", ETH_CS_PIN);
  Serial.printf("  INT:  GPIO %d\n", ETH_INT_PIN);
  Serial.printf("  RST:  GPIO %d\n\n", ETH_RST_PIN);

  Serial.println("SPI Configuration:");
  Serial.println("  Speed: 4 MHz");
  Serial.println("  Mode: 0 (CPOL=0, CPHA=0)");
  Serial.println("  Bit Order: MSB First\n");
}

void loop() {
  // Keep the TCP server alive before anything slow happens
  serviceSocket();

  // Check for serial commands (non-blocking)
  while (Serial.available() > 0) {
    char cmd = Serial.read();
    if (cmd == 's' || cmd == 'S') {
      Serial.println("\n[Command 'S' received - Setting IP configuration...]");
      setLinkLocalIP();
      // Clear any remaining characters
      while (Serial.available() > 0) Serial.read();
      return;  // Skip this cycle, will show new config on next refresh
    }
  }

  Serial.println("\n");
  boxBanner("W5500 DETAILED STATUS REPORT");
  Serial.println();

  // Read and verify chip version
  uint8_t version = w5500_read_byte(W5500_VERSIONR);
  boxTop("CHIP IDENTIFICATION");
  if (version == 0x04) {
    boxRow("%-21s 0x%02X  ✓ W5500 detected", "Chip Version:", version);
  } else if (version == 0x00 || version == 0xFF) {
    boxRow("%-21s 0x%02X  ✗ NO RESPONSE - check wiring!", "Chip Version:", version);
  } else {
    boxRow("%-21s 0x%02X  ? unexpected - wrong chip?", "Chip Version:", version);
  }
  boxBottom();
  Serial.println();

  // If chip not responding, skip the rest
  if (version != 0x04) {
    Serial.println("⚠ Cannot read configuration - chip not responding properly");
    Serial.println("");
    Serial.println("═══════════════════════════════════════════════════════════════════");
    Serial.println("");
    delay(5000);
    return;
  }

  // Read MAC Address
  uint8_t mac[6];
  w5500_read_bytes(W5500_SHAR, mac, 6);
  bool mac_blank = true;
  for (int i = 0; i < 6; i++) if (mac[i]) mac_blank = false;

  boxTop("MAC ADDRESS");
  boxRow("%-21s %02X:%02X:%02X:%02X:%02X:%02X%s", "MAC Address:",
         mac[0], mac[1], mac[2], mac[3], mac[4], mac[5],
         mac_blank ? "   (not configured)" : "");
  boxBottom();
  Serial.println();

  // Read IP Configuration
  uint8_t ip[4], gateway[4], subnet[4];
  w5500_read_bytes(W5500_SIPR, ip, 4);
  w5500_read_bytes(W5500_GAR, gateway, 4);
  w5500_read_bytes(W5500_SUBR, subnet, 4);

  boxTop("IP CONFIGURATION");
  boxRow("%-21s %d.%d.%d.%d%s", "IP Address:", ip[0], ip[1], ip[2], ip[3],
         (ip[0] | ip[1] | ip[2] | ip[3]) ? "" : "   (not configured)");
  boxRow("%-21s %d.%d.%d.%d%s", "Subnet Mask:",
         subnet[0], subnet[1], subnet[2], subnet[3],
         (subnet[0] | subnet[1] | subnet[2] | subnet[3]) ? "" : "   (not configured)");
  boxRow("%-21s %d.%d.%d.%d%s", "Gateway:",
         gateway[0], gateway[1], gateway[2], gateway[3],
         (gateway[0] | gateway[1] | gateway[2] | gateway[3])
           ? "" : "   (none - correct for link-local)");
  boxBottom();
  Serial.println();

  // Read PHY Status
  uint8_t phycfg = w5500_read_byte(W5500_PHYCFGR);
  bool link_up = phycfg & PHYCFGR_LNK;
  bool speed_100 = phycfg & PHYCFGR_SPD;
  bool full_duplex = phycfg & PHYCFGR_DPX;
  uint8_t opmode = (phycfg >> 3) & 0x07;

  // Detect link state changes
  if (link_up != last_link_state) {
    Serial.println(link_up ? "\n*** LINK UP - Cable connected! ***\n"
                           : "\n*** LINK DOWN - Cable disconnected! ***\n");
    last_link_state = link_up;
  }

  boxTop("PHY STATUS (Physical Layer)");
  boxRow("%-21s %s", "Link Status:", link_up ? "UP ✓" : "DOWN ✗");
  if (link_up) {
    boxRow("%-21s %s", "Speed:", speed_100 ? "100 Mbps" : "10 Mbps");
    boxRow("%-21s %s", "Duplex:", full_duplex ? "Full Duplex" : "Half Duplex");
  } else {
    boxRow("%-21s %s", "Speed:", "N/A (link down)");
    boxRow("%-21s %s", "Duplex:", "N/A (link down)");
  }

  const char* opmode_text;
  switch (opmode) {
    case 0:  opmode_text = "10BT half-duplex, auto-negotiation off";  break;
    case 1:  opmode_text = "10BT full-duplex, auto-negotiation off";  break;
    case 2:  opmode_text = "100BT half-duplex, auto-negotiation off"; break;
    case 3:  opmode_text = "100BT full-duplex, auto-negotiation off"; break;
    case 7:  opmode_text = "all capable, auto-negotiation enabled";   break;
    default: opmode_text = "unknown / reserved";                      break;
  }
  boxRow("%-21s 0x%02X  %s", "PHY Mode:", opmode, opmode_text);

  char bits[9];
  for (int i = 0; i < 8; i++) bits[i] = ((phycfg >> (7 - i)) & 1) ? '1' : '0';
  bits[8] = '\0';
  boxRow("%-21s 0b%s  (0x%02X)", "PHYCFGR Raw:", bits, phycfg);
  boxBottom();
  Serial.println();

  // Read interrupt status
  uint8_t int_reg = w5500_read_byte(W5500_IR);
  uint8_t int_mask = w5500_read_byte(W5500_IMR);
  uint8_t socket_int = w5500_read_byte(W5500_SIR);
  uint8_t socket_int_mask = w5500_read_byte(W5500_SIMR);
  uint8_t s0_int = w5500_read_socket_byte(0, W5500_Sn_IR);
  uint8_t s0_imr = w5500_read_socket_byte(0, W5500_Sn_IMR);
  uint8_t s0_status = w5500_read_socket_byte(0, W5500_Sn_SR);
  bool int_pin_state = digitalRead(ETH_INT_PIN);

  boxTop("INTERRUPT STATUS");
  boxRow("%-21s %s", "INT Pin State:",
         int_pin_state ? "HIGH (inactive)" : "LOW (ACTIVE!)");
  boxRow("%-21s %lu", "Interrupt Count:", interrupt_count);
  if (interrupt_occurred) {
    boxRow("*** NEW INTERRUPT DETECTED! ***");
    interrupt_occurred = false;  // Clear flag after displaying
  }
  boxRow("");

  boxRow("%-21s 0x%02X%s", "Common IR:", int_reg, int_reg ? "" : "  (no interrupts)");
  if (int_reg & IR_CONFLICT) boxRow("  └─ [CONFLICT] IP address conflict");
  if (int_reg & IR_UNREACH)  boxRow("  └─ [UNREACH] Destination unreachable");
  if (int_reg & IR_PPPoE)    boxRow("  └─ [PPPoE] PPPoE connection closed");
  if (int_reg & IR_MP)       boxRow("  └─ [MP] Magic packet received");
  boxRow("%-21s 0x%02X", "Common IMR (mask):", int_mask);
  boxRow("");

  boxRow("%-21s 0x%02X%s", "Socket SIR:", socket_int,
         socket_int ? "" : "  (no socket interrupts)");
  for (int i = 0; i < 8; i++) {
    if (socket_int & (1 << i)) boxRow("  └─ Socket %d has an interrupt", i);
  }
  boxRow("%-21s 0x%02X", "Socket SIMR (mask):", socket_int_mask);
  boxRow("");

  char imr_flags[48];
  snprintf(imr_flags, sizeof(imr_flags), "%s%s%s%s",
           (s0_imr & Sn_IR_CON)     ? "[CON] "     : "",
           (s0_imr & Sn_IR_DISCON)  ? "[DISCON] "  : "",
           (s0_imr & Sn_IR_RECV)    ? "[RECV] "    : "",
           (s0_imr & Sn_IR_TIMEOUT) ? "[TIMEOUT] " : "");
  boxRow("%-21s 0x%02X  %s", "Socket 0 Sn_IMR:", s0_imr, imr_flags);

  boxRow("%-21s 0x%02X%s", "Socket 0 Sn_IR:", s0_int, s0_int ? "" : "  (no interrupts)");
  if (s0_int & Sn_IR_CON) {
    boxRow("  └─ [CON] Connection established");
    w5500_write_socket_byte(0, W5500_Sn_IR, Sn_IR_CON);
  }
  if (s0_int & Sn_IR_DISCON) {
    boxRow("  └─ [DISCON] Disconnection occurred");
    w5500_write_socket_byte(0, W5500_Sn_IR, Sn_IR_DISCON);
  }
  if (s0_int & Sn_IR_RECV)    boxRow("  └─ [RECV] Data received");
  if (s0_int & Sn_IR_TIMEOUT) boxRow("  └─ [TIMEOUT] Timeout occurred");
  if (s0_int & Sn_IR_SENDOK)  boxRow("  └─ [SENDOK] Send completed");

  boxRow("%-21s 0x%02X  (%s)", "Socket 0 Status:", s0_status, sockStateName(s0_status));
  boxBottom();
  Serial.println();

  // TCP server summary
  boxTop("TCP SERVER");
  if (!net_configured) {
    boxRow("%-21s %s", "Server:", "NOT STARTED - press 'S' to configure");
  } else {
    boxRow("%-21s %d.%d.%d.%d:%d", "Listening on:", ip[0], ip[1], ip[2], ip[3], TCP_PORT);
    boxRow("%-21s %s", "Client:", client_connected ? "CONNECTED" : "none");
    boxRow("%-21s %lu", "Connections served:", conn_count);
    if (client_connected) {
      boxRow("%-21s %lu", "Bytes received:", rx_bytes);
      boxRow("%-21s %u waiting / %u free", "RX / TX buffer:",
             w5500_rx_available(0), w5500_tx_free(0));
    }
    boxRow("%-21s nc %d.%d.%d.%d %d", "Try:", ip[0], ip[1], ip[2], ip[3], TCP_PORT);
  }
  boxBottom();
  Serial.println();

  // Read additional registers
  uint8_t mode_reg = w5500_read_byte(W5500_MR);
  char mr_flags[48];
  snprintf(mr_flags, sizeof(mr_flags), "%s%s%s%s%s",
           (mode_reg & 0x80) ? "[RST] "   : "",
           (mode_reg & 0x20) ? "[WOL] "   : "",
           (mode_reg & 0x10) ? "[PB] "    : "",
           (mode_reg & 0x08) ? "[PPPoE] " : "",
           (mode_reg & 0x04) ? "[FARP] "  : "");

  boxTop("OTHER REGISTERS");
  boxRow("%-21s 0x%02X  %s", "Mode Register (MR):", mode_reg, mr_flags);
  boxBottom();

  Serial.println();
  Serial.println("═══════════════════════════════════════════════════════════════════");
  Serial.println();
  Serial.println("Commands: Type 'S' to set IP and start the TCP server on port 80");
  Serial.println("Next refresh in 5 seconds...");

  // Non-blocking delay - check serial during wait
  unsigned long startTime = millis();
  while (millis() - startTime < 5000) {
    if (Serial.available() > 0) {
      char cmd = Serial.read();
      if (cmd == 's' || cmd == 'S') {
        Serial.println("\n[Command 'S' received - Setting IP configuration...]");
        setLinkLocalIP();
        while (Serial.available() > 0) Serial.read();
        return;
      }
    }
    serviceSocket();  // accept, converse, close, re-listen
    delay(10);  // Small delay to prevent tight loop
  }
}

/**
 * Set link-local IP configuration for W5500
 * This allows the module to be pingable on the local network
 */
void setLinkLocalIP() {
  Serial.println("\n>>> Setting Link-Local IP Configuration...");

  // Link-local IP: 169.254.1.100
  uint8_t ip[4] = {169, 254, 120, 125};

  // Subnet mask: 255.255.0.0 (standard for link-local)
  uint8_t subnet[4] = {255, 255, 0, 0};

  // Gateway: 0.0.0.0 (no gateway for link-local)
  uint8_t gateway[4] = {0, 0, 0, 0};

  // MAC address: Use a locally administered MAC
  // First byte 0x02 = locally administered, unicast
  uint8_t mac[6] = {0x02, 0xDE, 0xAD, 0xBE, 0xEF, 0x42};

  // Write MAC address (required - W5500 has no built-in MAC)
  Serial.print("  Writing MAC:     ");
  w5500_write_bytes(W5500_SHAR, mac, 6);
  Serial.printf("%02X:%02X:%02X:%02X:%02X:%02X\n", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  // Write Gateway
  Serial.print("  Writing Gateway: ");
  w5500_write_bytes(W5500_GAR, gateway, 4);
  Serial.printf("%d.%d.%d.%d\n", gateway[0], gateway[1], gateway[2], gateway[3]);

  // Write Subnet
  Serial.print("  Writing Subnet:  ");
  w5500_write_bytes(W5500_SUBR, subnet, 4);
  Serial.printf("%d.%d.%d.%d\n", subnet[0], subnet[1], subnet[2], subnet[3]);

  // Write IP Address
  Serial.print("  Writing IP:      ");
  w5500_write_bytes(W5500_SIPR, ip, 4);
  Serial.printf("%d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);

  // Open TCP listening socket first
  openTCPSocket();

  // Enable interrupts on W5500 AFTER socket is open
  Serial.println(">>> Enabling W5500 interrupts...");

  // Enable socket 0 interrupts in global SIMR
  w5500_write_byte(W5500_SIMR, 0x01);  // Enable socket 0 interrupts
  Serial.println("    Socket SIMR: Socket 0 enabled");

  // Socket 0's own mask (Sn_IMR) is set inside openTCPSocket(), because the
  // OPEN command resets the socket register block and would wipe it.

  // Verify by reading back
  uint8_t simr_check = w5500_read_byte(W5500_SIMR);
  uint8_t s0_imr_check = w5500_read_socket_byte(0, W5500_Sn_IMR);
  Serial.printf("    Verified SIMR = 0x%02X, Sn_IMR = 0x%02X\n", simr_check, s0_imr_check);

  // Configure PHY for 10BASE-T Full-duplex (disable auto-negotiation)
  // PHYCFGR bits: [7]=RST, [6]=OPMD, [5:3]=OPMDC, [2]=DPX, [1]=SPD, [0]=LNK
  // OPMDC = 001 (0x08) = 10BT Full-duplex, auto-negotiation disabled
  // OPMD = 1 (0x40) = Configure operation mode
  // uint8_t phycfg = 0x40 | (1 << 3);  // OPMD=1, OPMDC=001 (10BT Full-duplex)
  // Serial.print("  Configuring PHY: ");
  // w5500_write_byte(W5500_PHYCFGR, phycfg);
  // Serial.printf("10BASE-T Full-duplex (0x%02X)\n", phycfg);

  net_configured = true;  // serviceSocket() may now drive the socket

  Serial.println(">>> Configuration complete!");
  Serial.printf(">>> The W5500 should now respond to pings at %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
  Serial.printf(">>> TCP server listening on port %d - connect with:\n", TCP_PORT);
  Serial.printf(">>>   nc %d.%d.%d.%d %d        (or: telnet %d.%d.%d.%d %d)\n",
                ip[0], ip[1], ip[2], ip[3], TCP_PORT,
                ip[0], ip[1], ip[2], ip[3], TCP_PORT);
  Serial.println(">>> Type 'help' once connected, 'quit' to disconnect.");
  Serial.println(">>> Reconnecting is fine - the socket re-arms after every close.\n");
  Serial.println(">>> (Make sure your computer is on the same link-local subnet)\n");

  delay(2000);  // Give user time to read the message
}

/**
 * Read a single byte from W5500 register
 *
 * W5500 SPI frame format:
 * - Byte 0-1: 16-bit address
 * - Byte 2: Control byte (block select + R/W mode)
 * - Byte 3+: Data
 */
uint8_t w5500_read_byte(uint16_t addr) {
  uint8_t control = (W5500_COMMON_REG << 3) | W5500_READ;

  digitalWrite(ETH_CS_PIN, LOW);

  // Send address (MSB first)
  SPI.transfer((addr >> 8) & 0xFF);  // Address high byte
  SPI.transfer(addr & 0xFF);          // Address low byte

  // Send control byte
  SPI.transfer(control);

  // Read data byte
  uint8_t data = SPI.transfer(0x00);

  digitalWrite(ETH_CS_PIN, HIGH);

  return data;
}

/**
 * Read multiple bytes from W5500 register
 */
void w5500_read_bytes(uint16_t addr, uint8_t* buf, uint16_t len) {
  uint8_t control = (W5500_COMMON_REG << 3) | W5500_READ;

  digitalWrite(ETH_CS_PIN, LOW);

  // Send address (MSB first)
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);

  // Send control byte
  SPI.transfer(control);

  // Read data bytes
  for (uint16_t i = 0; i < len; i++) {
    buf[i] = SPI.transfer(0x00);
  }

  digitalWrite(ETH_CS_PIN, HIGH);
}

/**
 * Write a single byte to W5500 register
 */
void w5500_write_byte(uint16_t addr, uint8_t data) {
  uint8_t control = (W5500_COMMON_REG << 3) | W5500_WRITE;

  digitalWrite(ETH_CS_PIN, LOW);

  // Send address (MSB first)
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);

  // Send control byte
  SPI.transfer(control);

  // Send data byte
  SPI.transfer(data);

  digitalWrite(ETH_CS_PIN, HIGH);
}

/**
 * Write multiple bytes to W5500 register
 */
void w5500_write_bytes(uint16_t addr, uint8_t* buf, uint16_t len) {
  uint8_t control = (W5500_COMMON_REG << 3) | W5500_WRITE;

  digitalWrite(ETH_CS_PIN, LOW);

  // Send address (MSB first)
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);

  // Send control byte
  SPI.transfer(control);

  // Write data bytes
  for (uint16_t i = 0; i < len; i++) {
    SPI.transfer(buf[i]);
  }

  digitalWrite(ETH_CS_PIN, HIGH);
}

/**
 * Read a run of bytes from any W5500 block (common, socket reg, socket buffer)
 */
void w5500_read_block(uint8_t block, uint16_t addr, uint8_t* buf, uint16_t len) {
  uint8_t control = (block << 3) | W5500_READ;

  digitalWrite(ETH_CS_PIN, LOW);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(control);
  for (uint16_t i = 0; i < len; i++) {
    buf[i] = SPI.transfer(0x00);
  }
  digitalWrite(ETH_CS_PIN, HIGH);
}

/**
 * Write a run of bytes to any W5500 block
 */
void w5500_write_block(uint8_t block, uint16_t addr, const uint8_t* buf, uint16_t len) {
  uint8_t control = (block << 3) | W5500_WRITE;

  digitalWrite(ETH_CS_PIN, LOW);
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);
  SPI.transfer(control);
  for (uint16_t i = 0; i < len; i++) {
    SPI.transfer(buf[i]);
  }
  digitalWrite(ETH_CS_PIN, HIGH);
}

/**
 * Read a 16-bit big-endian socket register
 */
uint16_t w5500_read_socket_word(uint8_t socket_num, uint16_t addr) {
  uint8_t b[2];
  w5500_read_block(BLOCK_SOCK_REG(socket_num), addr, b, 2);
  return ((uint16_t)b[0] << 8) | b[1];
}

/**
 * Write a 16-bit big-endian socket register
 */
void w5500_write_socket_word(uint8_t socket_num, uint16_t addr, uint16_t value) {
  uint8_t b[2] = { (uint8_t)(value >> 8), (uint8_t)(value & 0xFF) };
  w5500_write_block(BLOCK_SOCK_REG(socket_num), addr, b, 2);
}

/**
 * Bytes waiting in the socket RX buffer
 *
 * Sn_RX_RSR can change between the two byte reads of the 16-bit value, so read
 * it until two consecutive reads agree.
 */
uint16_t w5500_rx_available(uint8_t socket_num) {
  uint16_t prev, now = w5500_read_socket_word(socket_num, W5500_Sn_RX_RSR);
  do {
    prev = now;
    now = w5500_read_socket_word(socket_num, W5500_Sn_RX_RSR);
  } while (now != prev);
  return now;
}

/**
 * Free space in the socket TX buffer
 */
uint16_t w5500_tx_free(uint8_t socket_num) {
  uint16_t prev, now = w5500_read_socket_word(socket_num, W5500_Sn_TX_FSR);
  do {
    prev = now;
    now = w5500_read_socket_word(socket_num, W5500_Sn_TX_FSR);
  } while (now != prev);
  return now;
}

/**
 * Copy up to maxlen bytes out of the socket RX buffer
 *
 * The W5500 masks the buffer address internally, so the read pointer can be
 * used directly and wrap-around needs no special handling.
 */
uint16_t w5500_recv(uint8_t socket_num, uint8_t* buf, uint16_t maxlen) {
  uint16_t avail = w5500_rx_available(socket_num);
  if (avail == 0) return 0;

  uint16_t len = (avail > maxlen) ? maxlen : avail;
  uint16_t ptr = w5500_read_socket_word(socket_num, W5500_Sn_RX_RD);

  w5500_read_block(BLOCK_SOCK_RX(socket_num), ptr, buf, len);

  w5500_write_socket_word(socket_num, W5500_Sn_RX_RD, ptr + len);
  w5500_write_socket_byte(socket_num, W5500_Sn_CR, Sn_CR_RECV);
  w5500_write_socket_byte(socket_num, W5500_Sn_IR, Sn_IR_RECV);  // clear RECV flag

  return len;
}

/**
 * Write bytes into the socket TX buffer and send them
 *
 * Returns false on timeout or if the socket died mid-send. Sending is still
 * allowed in CLOSE_WAIT: the remote has stopped sending but can still receive.
 */
bool w5500_send(uint8_t socket_num, const uint8_t* data, uint16_t len) {
  if (len == 0) return true;

  unsigned long t0 = millis();
  while (w5500_tx_free(socket_num) < len) {
    uint8_t status = w5500_read_socket_byte(socket_num, W5500_Sn_SR);
    if (status != SOCK_ESTABLISHED && status != SOCK_CLOSE_WAIT) return false;
    if (millis() - t0 > SOCK_IO_TIMEOUT) return false;
    delay(1);
  }

  uint16_t ptr = w5500_read_socket_word(socket_num, W5500_Sn_TX_WR);
  w5500_write_block(BLOCK_SOCK_TX(socket_num), ptr, data, len);
  w5500_write_socket_word(socket_num, W5500_Sn_TX_WR, ptr + len);
  w5500_write_socket_byte(socket_num, W5500_Sn_CR, Sn_CR_SEND);

  // Sn_IR bits are set regardless of Sn_IMR, so polling works with or
  // without interrupts enabled
  t0 = millis();
  while (true) {
    uint8_t ir = w5500_read_socket_byte(socket_num, W5500_Sn_IR);
    if (ir & Sn_IR_SENDOK) {
      w5500_write_socket_byte(socket_num, W5500_Sn_IR, Sn_IR_SENDOK);
      return true;
    }
    if (ir & Sn_IR_TIMEOUT) {
      w5500_write_socket_byte(socket_num, W5500_Sn_IR, Sn_IR_TIMEOUT);
      return false;
    }
    if (w5500_read_socket_byte(socket_num, W5500_Sn_SR) == SOCK_CLOSED) return false;
    if (millis() - t0 > SOCK_IO_TIMEOUT) return false;
    delay(1);
  }
}

/**
 * Read a single byte from W5500 socket register
 * Socket n register block control byte: (socket_num * 4 + 1) << 3
 */
uint8_t w5500_read_socket_byte(uint8_t socket_num, uint16_t addr) {
  uint8_t control = ((socket_num * 4 + 1) << 3) | W5500_READ;

  digitalWrite(ETH_CS_PIN, LOW);

  // Send address (MSB first)
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);

  // Send control byte
  SPI.transfer(control);

  // Read data byte
  uint8_t data = SPI.transfer(0x00);

  digitalWrite(ETH_CS_PIN, HIGH);

  return data;
}

/**
 * Write a single byte to W5500 socket register
 * Socket n register block control byte: (socket_num * 4 + 1) << 3
 */
void w5500_write_socket_byte(uint8_t socket_num, uint16_t addr, uint8_t data) {
  uint8_t control = ((socket_num * 4 + 1) << 3) | W5500_WRITE;

  digitalWrite(ETH_CS_PIN, LOW);

  // Send address (MSB first)
  SPI.transfer((addr >> 8) & 0xFF);
  SPI.transfer(addr & 0xFF);

  // Send control byte
  SPI.transfer(control);

  // Send data byte
  SPI.transfer(data);

  digitalWrite(ETH_CS_PIN, HIGH);
}

/**
 * Open (or re-arm) the TCP listening socket on port 80
 *
 * The W5500 does NOT go back to LISTEN by itself after a connection ends -
 * every connection needs a fresh OPEN + LISTEN pair, so this is called again
 * from serviceSocket() each time the socket falls back to CLOSED.
 */
void openTCPSocket(bool verbose) {
  if (verbose) Serial.printf("\n>>> Opening TCP socket on port %d...\n", TCP_PORT);

  // Make sure we start from CLOSED, whatever state the socket was left in
  w5500_write_socket_byte(0, W5500_Sn_CR, Sn_CR_CLOSE);
  delay(2);
  w5500_write_socket_byte(0, W5500_Sn_IR, 0xFF);  // clear stale socket interrupts

  // Set socket 0 to TCP mode
  w5500_write_socket_byte(0, W5500_Sn_MR, Sn_MR_TCP);

  // Set the listening port
  w5500_write_socket_word(0, W5500_Sn_PORT, TCP_PORT);

  // Send OPEN command
  w5500_write_socket_byte(0, W5500_Sn_CR, Sn_CR_OPEN);
  delay(5);

  uint8_t status = w5500_read_socket_byte(0, W5500_Sn_SR);
  if (status != SOCK_INIT) {
    Serial.printf("    ERROR: Expected INIT state, got 0x%02X (%s)\n",
                  status, sockStateName(status));
    return;
  }

  // OPEN resets the socket register block, so (re)apply the interrupt mask here
  w5500_write_socket_byte(0, W5500_Sn_IMR,
                          Sn_IR_CON | Sn_IR_DISCON | Sn_IR_RECV | Sn_IR_TIMEOUT);

  // Send LISTEN command
  w5500_write_socket_byte(0, W5500_Sn_CR, Sn_CR_LISTEN);
  delay(5);

  status = w5500_read_socket_byte(0, W5500_Sn_SR);
  if (status != SOCK_LISTEN) {
    Serial.printf("    ERROR: Expected LISTEN state, got 0x%02X (%s)\n",
                  status, sockStateName(status));
    return;
  }

  if (verbose) {
    Serial.printf("    Socket listening on port %d\n", TCP_PORT);
    Serial.println("\n*** Connect and type 'help' - try: nc 169.254.120.125 80 ***\n");
  } else {
    Serial.println("[TCP] Listening again");
  }
  last_arm_attempt = millis();
}

/**
 * Human readable socket status
 */
const char* sockStateName(uint8_t status) {
  switch (status) {
    case SOCK_CLOSED:      return "CLOSED";
    case SOCK_INIT:        return "INIT";
    case SOCK_LISTEN:      return "LISTEN";
    case SOCK_SYNSENT:     return "SYNSENT";
    case SOCK_SYNRECV:     return "SYNRECV";
    case SOCK_ESTABLISHED: return "ESTABLISHED";
    case SOCK_FIN_WAIT:    return "FIN_WAIT";
    case SOCK_CLOSING:     return "CLOSING";
    case SOCK_TIME_WAIT:   return "TIME_WAIT";
    case SOCK_CLOSE_WAIT:  return "CLOSE_WAIT";
    case SOCK_LAST_ACK:    return "LAST_ACK";
    default:               return "UNKNOWN";
  }
}

/**
 * Drive the TCP server state machine
 *
 * Call this often (it is cheap - a couple of SPI reads when idle). It accepts a
 * connection, feeds received bytes to the command parser, closes cleanly, and
 * puts the socket back into LISTEN for the next client.
 */
void serviceSocket() {
  if (!net_configured) return;

  uint8_t status = w5500_read_socket_byte(0, W5500_Sn_SR);

  switch (status) {
    case SOCK_CLOSED:
      // Nothing is listening - re-arm the server for the next client.
      // Throttled so a failing open (link down, for example) cannot flood
      // the serial log with retries.
      if (millis() - last_arm_attempt >= 250) {
        last_arm_attempt = millis();
        openTCPSocket(false);
      }
      break;

    case SOCK_INIT:
      // Opened but not listening yet
      if (millis() - last_arm_attempt >= 250) {
        last_arm_attempt = millis();
        w5500_write_socket_byte(0, W5500_Sn_CR, Sn_CR_LISTEN);
        delay(2);
      }
      break;

    case SOCK_LISTEN:
      break;  // idle, waiting for a SYN

    case SOCK_ESTABLISHED:
      if (!client_connected) {
        client_connected = true;
        conn_count++;
        rx_bytes = 0;
        cmd_len = 0;
        iac_state = 0;
        want_close = false;
        Serial.printf("\n[TCP] Client connected (connection #%lu)\n", conn_count);
        sendStr("\r\n=== W5500 SPI Test ===\r\n"
                "Type 'help' for commands, 'quit' to disconnect.\r\n");
        sendPrompt();
      }
      drainRx();
      if (want_close) {
        sendStr("Bye.\r\n");
        closeConnection("quit command");
      }
      break;

    case SOCK_CLOSE_WAIT:
      // The remote end sent FIN. Take anything still buffered, then finish up.
      drainRx();
      closeConnection("remote closed");
      break;

    default:
      // Transient states (SYNRECV, FIN_WAIT, CLOSING, TIME_WAIT, LAST_ACK):
      // let the chip work through them on its own.
      break;
  }
}

/**
 * Close the active connection and go back to listening
 */
void closeConnection(const char* reason) {
  Serial.printf("[TCP] Closing connection (%s)\n", reason);

  // DISCON sends FIN and performs the full handshake
  w5500_write_socket_byte(0, W5500_Sn_CR, Sn_CR_DISCON);

  unsigned long t0 = millis();
  while (millis() - t0 < 500) {
    uint8_t status = w5500_read_socket_byte(0, W5500_Sn_SR);
    if (status == SOCK_CLOSED) break;
    uint8_t ir = w5500_read_socket_byte(0, W5500_Sn_IR);
    if (ir & Sn_IR_TIMEOUT) break;  // peer vanished - stop waiting
    delay(5);
  }

  // CLOSE forces the socket down even if the handshake did not complete
  w5500_write_socket_byte(0, W5500_Sn_CR, Sn_CR_CLOSE);
  delay(2);
  w5500_write_socket_byte(0, W5500_Sn_IR, 0xFF);

  client_connected = false;
  want_close = false;
  cmd_len = 0;
  iac_state = 0;

  openTCPSocket(false);  // ready for the next client
}

/**
 * Pull everything waiting in the RX buffer through the command parser
 */
void drainRx() {
  uint8_t buf[64];
  uint16_t n;
  while ((n = w5500_recv(0, buf, sizeof(buf))) > 0) {
    rx_bytes += n;
    for (uint16_t i = 0; i < n && !want_close; i++) {
      feedByte(buf[i]);
    }
    if (want_close) break;
  }
}

/**
 * Feed one received byte into the line assembler
 *
 * Also swallows telnet IAC negotiation, which the telnet client sends as soon
 * as it connects. Without this the first command line arrives full of 0xFF
 * option bytes. 'nc' sends raw bytes and is unaffected.
 */
void feedByte(uint8_t c) {
  switch (iac_state) {
    case 1:  // previous byte was IAC (0xFF)
      if (c == 250)                   iac_state = 3;  // SB - subnegotiation follows
      else if (c >= 251 && c <= 254)  iac_state = 2;  // WILL/WONT/DO/DONT + option
      else                            iac_state = 0;  // two byte command, done
      return;
    case 2:  // option byte of WILL/WONT/DO/DONT
      iac_state = 0;
      return;
    case 3:  // inside subnegotiation, ends with IAC SE
      if (c == 255) iac_state = 4;
      return;
    case 4:
      iac_state = (c == 240) ? 0 : 3;  // SE ends it, anything else is escaped data
      return;
    default:
      break;
  }

  if (c == 255) { iac_state = 1; return; }  // IAC

  if (c == '\n') {
    cmd_buf[cmd_len] = '\0';
    handleLine(cmd_buf);
    cmd_len = 0;
    return;
  }
  if (c == '\r') return;                                  // wait for the LF
  if (c == 0x08 || c == 0x7F) {                            // backspace / delete
    if (cmd_len > 0) cmd_len--;
    return;
  }
  if (c < 32) return;                                      // ignore other controls
  if (cmd_len < CMD_BUF_LEN - 1) cmd_buf[cmd_len++] = (char)c;
}

/**
 * Execute one command line received over TCP
 */
void handleLine(char* line) {
  while (*line == ' ' || *line == '\t') line++;  // trim leading space

  // Split into command and argument
  char* arg = line;
  while (*arg && *arg != ' ' && *arg != '\t') arg++;
  if (*arg) { *arg = '\0'; arg++; while (*arg == ' ' || *arg == '\t') arg++; }

  if (*line == '\0') { sendPrompt(); return; }

  Serial.printf("[TCP] > %s%s%s\n", line, *arg ? " " : "", arg);

  if (!strcasecmp(line, "help") || !strcmp(line, "?")) {
    sendStr("Commands:\r\n"
            "  help          this list\r\n"
            "  status        chip version, link, socket state\r\n"
            "  ip            IP / subnet / gateway / MAC\r\n"
            "  phy           PHYCFGR decoded\r\n"
            "  ints          interrupt counters and registers\r\n"
            "  sock          socket 0 registers and buffer levels\r\n"
            "  uptime        milliseconds since boot\r\n"
            "  echo <text>   send <text> back\r\n"
            "  quit          close the connection\r\n");

  } else if (!strcasecmp(line, "status")) {
    uint8_t version = w5500_read_byte(W5500_VERSIONR);
    uint8_t phycfg = w5500_read_byte(W5500_PHYCFGR);
    uint8_t status = w5500_read_socket_byte(0, W5500_Sn_SR);
    sendFmt("Chip version : 0x%02X %s\r\n", version,
            version == 0x04 ? "(W5500)" : "(unexpected!)");
    sendFmt("Link         : %s", (phycfg & PHYCFGR_LNK) ? "UP" : "DOWN");
    if (phycfg & PHYCFGR_LNK) {
      sendFmt(" %s %s", (phycfg & PHYCFGR_SPD) ? "100Mbps" : "10Mbps",
              (phycfg & PHYCFGR_DPX) ? "full-duplex" : "half-duplex");
    }
    sendStr("\r\n");
    sendFmt("Socket 0     : 0x%02X (%s)\r\n", status, sockStateName(status));
    sendFmt("Connections  : %lu since boot\r\n", conn_count);
    sendFmt("Uptime       : %lu ms\r\n", millis());

  } else if (!strcasecmp(line, "ip")) {
    uint8_t ip[4], subnet[4], gateway[4], mac[6];
    w5500_read_bytes(W5500_SIPR, ip, 4);
    w5500_read_bytes(W5500_SUBR, subnet, 4);
    w5500_read_bytes(W5500_GAR, gateway, 4);
    w5500_read_bytes(W5500_SHAR, mac, 6);
    sendFmt("IP      : %d.%d.%d.%d\r\n", ip[0], ip[1], ip[2], ip[3]);
    sendFmt("Subnet  : %d.%d.%d.%d\r\n", subnet[0], subnet[1], subnet[2], subnet[3]);
    sendFmt("Gateway : %d.%d.%d.%d\r\n", gateway[0], gateway[1], gateway[2], gateway[3]);
    sendFmt("MAC     : %02X:%02X:%02X:%02X:%02X:%02X\r\n",
            mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

  } else if (!strcasecmp(line, "phy")) {
    uint8_t phycfg = w5500_read_byte(W5500_PHYCFGR);
    sendFmt("PHYCFGR : 0x%02X\r\n", phycfg);
    sendFmt("  Link   : %s\r\n", (phycfg & PHYCFGR_LNK) ? "UP" : "DOWN");
    sendFmt("  Speed  : %s\r\n", (phycfg & PHYCFGR_SPD) ? "100 Mbps" : "10 Mbps");
    sendFmt("  Duplex : %s\r\n", (phycfg & PHYCFGR_DPX) ? "Full" : "Half");
    sendFmt("  OPMDC  : %d%s\r\n", (phycfg >> 3) & 0x07,
            ((phycfg >> 3) & 0x07) == 7 ? " (auto-negotiation)" : " (forced)");

  } else if (!strcasecmp(line, "ints")) {
    sendFmt("INT pin       : %s\r\n",
            digitalRead(ETH_INT_PIN) ? "HIGH (inactive)" : "LOW (active)");
    sendFmt("ISR count     : %lu\r\n", interrupt_count);
    sendFmt("Last ISR      : %lu ms ago\r\n",
            last_interrupt_time ? millis() - last_interrupt_time : 0);
    sendFmt("Common IR     : 0x%02X\r\n", w5500_read_byte(W5500_IR));
    sendFmt("Common IMR    : 0x%02X\r\n", w5500_read_byte(W5500_IMR));
    sendFmt("Socket SIR    : 0x%02X\r\n", w5500_read_byte(W5500_SIR));
    sendFmt("Socket SIMR   : 0x%02X\r\n", w5500_read_byte(W5500_SIMR));
    sendFmt("Socket 0 Sn_IR : 0x%02X\r\n", w5500_read_socket_byte(0, W5500_Sn_IR));
    sendFmt("Socket 0 Sn_IMR: 0x%02X\r\n", w5500_read_socket_byte(0, W5500_Sn_IMR));

  } else if (!strcasecmp(line, "sock")) {
    uint8_t status = w5500_read_socket_byte(0, W5500_Sn_SR);
    sendFmt("Sn_SR    : 0x%02X (%s)\r\n", status, sockStateName(status));
    sendFmt("Sn_MR    : 0x%02X\r\n", w5500_read_socket_byte(0, W5500_Sn_MR));
    sendFmt("Sn_PORT  : %u\r\n", w5500_read_socket_word(0, W5500_Sn_PORT));
    sendFmt("RX buf   : %u KB, %u bytes waiting\r\n",
            w5500_read_socket_byte(0, W5500_Sn_RXBUF_SZ), w5500_rx_available(0));
    sendFmt("TX buf   : %u KB, %u bytes free\r\n",
            w5500_read_socket_byte(0, W5500_Sn_TXBUF_SZ), w5500_tx_free(0));
    sendFmt("RX_RD/WR : 0x%04X / 0x%04X\r\n",
            w5500_read_socket_word(0, W5500_Sn_RX_RD),
            w5500_read_socket_word(0, W5500_Sn_RX_WR));
    sendFmt("TX_RD/WR : 0x%04X / 0x%04X\r\n",
            w5500_read_socket_word(0, W5500_Sn_TX_RD),
            w5500_read_socket_word(0, W5500_Sn_TX_WR));
    sendFmt("Bytes in : %lu this connection\r\n", rx_bytes);

  } else if (!strcasecmp(line, "uptime")) {
    sendFmt("%lu ms\r\n", millis());

  } else if (!strcasecmp(line, "echo")) {
    sendFmt("%s\r\n", arg);

  } else if (!strcasecmp(line, "quit") || !strcasecmp(line, "exit") ||
             !strcasecmp(line, "close")) {
    want_close = true;
    return;  // no prompt - serviceSocket() is about to close us

  } else {
    sendFmt("Unknown command '%s' - try 'help'\r\n", line);
  }

  sendPrompt();
}

void sendPrompt() {
  sendStr("w5500> ");
}

void sendStr(const char* str) {
  w5500_send(0, (const uint8_t*)str, strlen(str));
}

void sendFmt(const char* fmt, ...) {
  char buf[256];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  sendStr(buf);
}

/**
 * Display width of a UTF-8 string
 *
 * The box drawing characters and the tick/cross marks are multi-byte, so
 * padding on strlen() would push every row that contains one out of line.
 * Counting only the bytes that are not UTF-8 continuation bytes gives the
 * column count for everything used here.
 */
int utf8Width(const char* str) {
  int width = 0;
  for (const char* p = str; *p; p++) {
    if ((*p & 0xC0) != 0x80) width++;
  }
  return width;
}

/**
 * Print one row of the status box, padded so the right border lines up
 */
void boxRow(const char* fmt, ...) {
  char buf[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);

  Serial.print("│ ");
  Serial.print(buf);
  for (int i = utf8Width(buf); i < BOX_W - 1; i++) Serial.print(' ');
  Serial.println("│");
}

/**
 * Print a box top with an inline title, sized to the title
 */
void boxTop(const char* title) {
  Serial.print("┌─ ");
  Serial.print(title);
  Serial.print(' ');
  for (int i = 3 + utf8Width(title); i < BOX_W; i++) Serial.print("─");
  Serial.println("┐");
}

/**
 * Print a box bottom
 */
void boxBottom() {
  Serial.print("└");
  for (int i = 0; i < BOX_W; i++) Serial.print("─");
  Serial.println("┘");
}

/**
 * Print a double-ruled banner with a centred title
 */
void boxBanner(const char* title) {
  Serial.print("╔");
  for (int i = 0; i < BOX_W; i++) Serial.print("═");
  Serial.println("╗");

  int width = utf8Width(title);
  int left = (BOX_W - width) / 2;
  Serial.print("║");
  for (int i = 0; i < left; i++) Serial.print(' ');
  Serial.print(title);
  for (int i = left + width; i < BOX_W; i++) Serial.print(' ');
  Serial.println("║");

  Serial.print("╚");
  for (int i = 0; i < BOX_W; i++) Serial.print("═");
  Serial.println("╝");
}

/**
 * W5500 Interrupt Service Routine
 * Called when INT pin goes LOW (active low interrupt)
 */
void IRAM_ATTR w5500_isr() {
  interrupt_occurred = true;
  interrupt_count++;
  last_interrupt_time = millis();
}
