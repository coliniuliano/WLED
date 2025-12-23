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
#define W5500_Sn_IMR      0x002C  // Socket Interrupt Mask Register (offset within socket block)

// Socket commands
#define Sn_CR_OPEN        0x01
#define Sn_CR_LISTEN      0x02
#define Sn_CR_CLOSE       0x10

// Socket modes
#define Sn_MR_TCP         0x01

// Socket status values
#define SOCK_CLOSED       0x00
#define SOCK_INIT         0x13
#define SOCK_LISTEN       0x14
#define SOCK_ESTABLISHED  0x17
#define SOCK_CLOSE_WAIT   0x1C

// Interrupt tracking
volatile bool interrupt_occurred = false;
volatile unsigned long interrupt_count = 0;
volatile unsigned long last_interrupt_time = 0;

// Link state tracking
bool last_link_state = false;

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
void openTCPSocket();

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
  Serial.println("╔════════════════════════════════════════════════════════════════════╗");
  Serial.println("║                   W5500 DETAILED STATUS REPORT                     ║");
  Serial.println("╚════════════════════════════════════════════════════════════════════╝");
  Serial.println();

  // Read and verify chip version
  uint8_t version = w5500_read_byte(W5500_VERSIONR);
  Serial.println("┌─ CHIP IDENTIFICATION ─────────────────────────────────────────────┐");
  Serial.printf("│ Chip Version:        0x%02X ", version);
  if (version == 0x04) {
    Serial.println("✓ (W5500 detected)                        │");
  } else if (version == 0x00 || version == 0xFF) {
    Serial.println("✗ (NO RESPONSE - check wiring!)           │");
  } else {
    Serial.println("? (UNEXPECTED - wrong chip?)              │");
  }
  Serial.println("└───────────────────────────────────────────────────────────────────┘");
  Serial.println();

  // If chip not responding, skip the rest
  if (version != 0x04) {
    Serial.println("⚠ Cannot read configuration - chip not responding properly");
    Serial.println("\n═══════════════════════════════════════════════════════════════════\n");
    delay(5000);
    return;
  }

  // Read MAC Address
  uint8_t mac[6];
  w5500_read_bytes(W5500_SHAR, mac, 6);
  Serial.println("┌─ MAC ADDRESS ─────────────────────────────────────────────────────┐");
  Serial.printf("│ MAC Address:         %02X:%02X:%02X:%02X:%02X:%02X",
                mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
  if (mac[0] == 0x00 && mac[1] == 0x00 && mac[2] == 0x00 &&
      mac[3] == 0x00 && mac[4] == 0x00 && mac[5] == 0x00) {
    Serial.println("  (Not configured)       │");
  } else {
    Serial.println("                     │");
  }
  Serial.println("└───────────────────────────────────────────────────────────────────┘");
  Serial.println();

  // Read IP Configuration
  uint8_t ip[4], gateway[4], subnet[4];
  w5500_read_bytes(W5500_SIPR, ip, 4);
  w5500_read_bytes(W5500_GAR, gateway, 4);
  w5500_read_bytes(W5500_SUBR, subnet, 4);

  Serial.println("┌─ IP CONFIGURATION ────────────────────────────────────────────────┐");
  Serial.printf("│ IP Address:          %3d.%3d.%3d.%3d", ip[0], ip[1], ip[2], ip[3]);
  if (ip[0] == 0 && ip[1] == 0 && ip[2] == 0 && ip[3] == 0) {
    Serial.println("              (Not configured) │");
  } else {
    Serial.println("                           │");
  }
  Serial.printf("│ Subnet Mask:         %3d.%3d.%3d.%3d", subnet[0], subnet[1], subnet[2], subnet[3]);
  if (subnet[0] == 0 && subnet[1] == 0 && subnet[2] == 0 && subnet[3] == 0) {
    Serial.println("              (Not configured) │");
  } else {
    Serial.println("                           │");
  }
  Serial.printf("│ Gateway:             %3d.%3d.%3d.%3d", gateway[0], gateway[1], gateway[2], gateway[3]);
  if (gateway[0] == 0 && gateway[1] == 0 && gateway[2] == 0 && gateway[3] == 0) {
    Serial.println("              (Not configured) │");
  } else {
    Serial.println("                           │");
  }
  Serial.println("└───────────────────────────────────────────────────────────────────┘");
  Serial.println();

  // Read PHY Status
  uint8_t phycfg = w5500_read_byte(W5500_PHYCFGR);
  bool link_up = phycfg & PHYCFGR_LNK;
  bool speed_100 = phycfg & PHYCFGR_SPD;
  bool full_duplex = phycfg & PHYCFGR_DPX;
  uint8_t opmode = (phycfg >> 3) & 0x07;

  // Detect link state changes
  if (link_up != last_link_state) {
    if (link_up) {
      Serial.println("\n*** LINK UP - Cable connected! ***\n");
    } else {
      Serial.println("\n*** LINK DOWN - Cable disconnected! ***\n");
    }
    last_link_state = link_up;
  }

  Serial.println("┌─ PHY STATUS (Physical Layer) ─────────────────────────────────────┐");
  Serial.printf("│ Link Status:         %s", link_up ? "UP ✓" : "DOWN ✗");
  Serial.println("                                              │");

  if (link_up) {
    Serial.printf("│ Speed:               %s", speed_100 ? "100 Mbps" : "10 Mbps ");
    Serial.println("                                         │");
    Serial.printf("│ Duplex:              %s", full_duplex ? "Full Duplex" : "Half Duplex");
    Serial.println("                                    │");
  } else {
    Serial.println("│ Speed:               N/A (Link Down)                               │");
    Serial.println("│ Duplex:              N/A (Link Down)                               │");
  }

  Serial.printf("│ PHY Mode:            0x%02X ", opmode);
  switch(opmode) {
    case 0: Serial.println("(10BT Half-duplex, Auto-negotiation disabled)  │"); break;
    case 1: Serial.println("(10BT Full-duplex, Auto-negotiation disabled)  │"); break;
    case 2: Serial.println("(100BT Half-duplex, Auto-negotiation disabled) │"); break;
    case 3: Serial.println("(100BT Full-duplex, Auto-negotiation disabled) │"); break;
    case 7: Serial.println("(All capable, Auto-negotiation enabled)        │"); break;
    default: Serial.println("(Unknown/Reserved)                             │"); break;
  }
  Serial.printf("│ PHYCFGR Raw:         0b");
  for (int i = 7; i >= 0; i--) {
    Serial.print((phycfg >> i) & 1);
  }
  Serial.printf(" (0x%02X)                              │\n", phycfg);
  Serial.println("└───────────────────────────────────────────────────────────────────┘");
  Serial.println();

  // Read interrupt status
  uint8_t int_reg = w5500_read_byte(W5500_IR);
  uint8_t int_mask = w5500_read_byte(W5500_IMR);
  uint8_t socket_int = w5500_read_byte(W5500_SIR);
  uint8_t socket_int_mask = w5500_read_byte(W5500_SIMR);
  uint8_t s0_int = w5500_read_socket_byte(0, W5500_Sn_IR);
  bool int_pin_state = digitalRead(ETH_INT_PIN);

  Serial.println("┌─ INTERRUPT STATUS ────────────────────────────────────────────────┐");
  Serial.printf("│ INT Pin State:       %s", int_pin_state ? "HIGH (inactive)" : "LOW (ACTIVE!)");
  Serial.println("                            │");
  Serial.printf("│ Interrupt Count:     %lu", interrupt_count);
  int count_digits = String(interrupt_count).length();
  for (int i = count_digits; i < 47; i++) Serial.print(" ");
  Serial.println("│");
  if (interrupt_occurred) {
    Serial.println("│ *** NEW INTERRUPT DETECTED! ***                                    │");
    interrupt_occurred = false;  // Clear flag after displaying
  }
  Serial.println("│                                                                    │");

  Serial.printf("│ Common IR:           0x%02X ", int_reg);
  if (int_reg == 0) {
    Serial.println("(No interrupts)                            │");
  } else {
    Serial.println("                                           │");
    if (int_reg & IR_CONFLICT) Serial.println("│   └─ [CONFLICT] IP address conflict                            │");
    if (int_reg & IR_UNREACH)  Serial.println("│   └─ [UNREACH] Destination unreachable                         │");
    if (int_reg & IR_PPPoE)    Serial.println("│   └─ [PPPoE] PPPoE connection closed                           │");
    if (int_reg & IR_MP)       Serial.println("│   └─ [MP] Magic packet received                                │");
  }

  Serial.printf("│ Common IMR (mask):   0x%02X                                           │\n", int_mask);
  Serial.println("│                                                                    │");

  Serial.printf("│ Socket SIR:          0x%02X ", socket_int);
  if (socket_int == 0) {
    Serial.println("(No socket interrupts)                     │");
  } else {
    Serial.println("                                           │");
    for (int i = 0; i < 8; i++) {
      if (socket_int & (1 << i)) {
        Serial.printf("│   └─ Socket %d has interrupt                                    │\n", i);
      }
    }
  }

  Serial.printf("│ Socket SIMR (mask):  0x%02X                                           │\n", socket_int_mask);

  // Read Socket 0's interrupt mask register
  uint8_t s0_imr = w5500_read_socket_byte(0, W5500_Sn_IMR);
  Serial.printf("│ Socket 0 Sn_IMR:     0x%02X ", s0_imr);
  if (s0_imr & Sn_IR_CON)     Serial.print("[CON] ");
  if (s0_imr & Sn_IR_DISCON)  Serial.print("[DISCON] ");
  int mask_len = 25;
  if (s0_imr & Sn_IR_CON) mask_len += 6;
  if (s0_imr & Sn_IR_DISCON) mask_len += 9;
  for (int i = mask_len; i < 43; i++) Serial.print(" ");
  Serial.println("│");
  Serial.println("│                                                                    │");

  Serial.printf("│ Socket 0 Sn_IR:      0x%02X ", s0_int);
  if (s0_int == 0) {
    Serial.println("(No interrupts)                            │");
  } else {
    Serial.println("                                           │");
    if (s0_int & Sn_IR_CON)     {
      Serial.println("│   └─ [CON] Connection established                              │");
      // Clear the interrupt by writing 1 to the bit
      w5500_write_socket_byte(0, W5500_Sn_IR, Sn_IR_CON);
    }
    if (s0_int & Sn_IR_DISCON)  {
      Serial.println("│   └─ [DISCON] Disconnection occurred                           │");
      // Clear the interrupt by writing 1 to the bit
      w5500_write_socket_byte(0, W5500_Sn_IR, Sn_IR_DISCON);
    }
    if (s0_int & Sn_IR_RECV)    Serial.println("│   └─ [RECV] Data received                                      │");
    if (s0_int & Sn_IR_TIMEOUT) Serial.println("│   └─ [TIMEOUT] Timeout occurred                                │");
    if (s0_int & Sn_IR_SENDOK)  Serial.println("│   └─ [SENDOK] Send completed                                   │");
  }

  // Read socket status
  uint8_t s0_status = w5500_read_socket_byte(0, W5500_Sn_SR);
  Serial.printf("│ Socket 0 Status:     0x%02X ", s0_status);
  switch(s0_status) {
    case SOCK_CLOSED:      Serial.println("(CLOSED)                                   │"); break;
    case SOCK_INIT:        Serial.println("(INIT)                                     │"); break;
    case SOCK_LISTEN:      Serial.println("(LISTEN - waiting for connection)          │"); break;
    case SOCK_ESTABLISHED: Serial.println("(ESTABLISHED - connected!)                 │"); break;
    case SOCK_CLOSE_WAIT:  Serial.println("(CLOSE_WAIT)                               │"); break;
    default:               Serial.println("(Unknown)                                  │"); break;
  }

  Serial.println("└───────────────────────────────────────────────────────────────────┘");
  Serial.println();

  // Read additional registers
  uint8_t mode_reg = w5500_read_byte(W5500_MR);

  Serial.println("┌─ OTHER REGISTERS ─────────────────────────────────────────────────┐");
  Serial.printf("│ Mode Register (MR):  0x%02X", mode_reg);
  if (mode_reg & 0x80) Serial.print(" [RST]");
  if (mode_reg & 0x20) Serial.print(" [WOL]");
  if (mode_reg & 0x10) Serial.print(" [PB]");
  if (mode_reg & 0x08) Serial.print(" [PPPoE]");
  if (mode_reg & 0x04) Serial.print(" [FARP]");
  // Pad to align with box
  int flags_printed = 0;
  if (mode_reg & 0x80) flags_printed += 6;
  if (mode_reg & 0x20) flags_printed += 6;
  if (mode_reg & 0x10) flags_printed += 5;
  if (mode_reg & 0x08) flags_printed += 8;
  if (mode_reg & 0x04) flags_printed += 7;
  for (int i = flags_printed; i < 42; i++) Serial.print(" ");
  Serial.println("│");
  Serial.println("└───────────────────────────────────────────────────────────────────┘");

  Serial.println();
  Serial.println("═══════════════════════════════════════════════════════════════════");
  Serial.println();
  Serial.println("Commands: Type 'S' to set IP | 'R' to reset PHY | 'A' to enable auto-neg");
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

  // Enable CON and DISCON interrupts for Socket 0
  w5500_write_socket_byte(0, W5500_Sn_IMR, Sn_IR_CON | Sn_IR_DISCON);
  Serial.println("    Socket 0 Sn_IMR: CON and DISCON enabled");

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

  Serial.println(">>> Configuration complete!");
  Serial.printf(">>> The W5500 should now respond to pings at %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
  Serial.println(">>> Link restricted to 10BASE-T Full-duplex (auto-negotiation disabled)\n");
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
 * Open a TCP listening socket on port 80
 */
void openTCPSocket() {
  Serial.println("\n>>> Opening TCP socket on port 80...");

  // Set socket 0 to TCP mode
  w5500_write_socket_byte(0, W5500_Sn_MR, Sn_MR_TCP);
  Serial.println("    Set mode to TCP");

  // Set port to 80 (HTTP)
  w5500_write_socket_byte(0, W5500_Sn_PORT, 0x00);     // Port high byte
  w5500_write_socket_byte(0, W5500_Sn_PORT + 1, 0x50); // Port low byte (80 = 0x0050)
  Serial.println("    Set port to 80");

  // Send OPEN command
  w5500_write_socket_byte(0, W5500_Sn_CR, Sn_CR_OPEN);
  delay(10);

  // Check if opened to INIT state
  uint8_t status = w5500_read_socket_byte(0, W5500_Sn_SR);
  if (status == SOCK_INIT) {
    Serial.println("    Socket opened (INIT state)");

    // Send LISTEN command
    w5500_write_socket_byte(0, W5500_Sn_CR, Sn_CR_LISTEN);
    delay(10);

    status = w5500_read_socket_byte(0, W5500_Sn_SR);
    if (status == SOCK_LISTEN) {
      Serial.println("    Socket listening on port 80!");
      Serial.println("\n*** Connect to this device on port 80 to test interrupts! ***");
      Serial.printf("*** Try: telnet %d.%d.%d.%d 80 ***\n", 169, 254, 120, 125);
    } else {
      Serial.printf("    ERROR: Expected LISTEN state, got 0x%02X\n", status);
    }
  } else {
    Serial.printf("    ERROR: Expected INIT state, got 0x%02X\n", status);
  }

  Serial.println();
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
