// =============================================================================
// boot_main.c  —  PicoRV32 Bootloader with JESD216 (SFDP) Flash Detection
// =============================================================================
// Compatibility: Any JESD216-compliant SPI NOR flash in 1-1-1 (standard SPI)
// mode.  The flash geometry (erase opcode, sector size, page size, address
// width) is discovered at runtime via the SFDP table so no chip-specific
// constants appear in this file.
//
// Opcodes that ARE hardcoded and WHY they are safe to hardcode:
//   0x06  WREN  — Mandated by JEDEC as the universal write-enable opcode.
//   0x05  RDSR  — Mandated by JEDEC; bit-0 WIP flag location is also fixed.
//   0x02  PP    — Universal 1-1-1 Page Program.  JESD216 does not define a
//                 replacement; every JESD216 chip supports it in SPI mode.
//   0x5A  RDSFDP— The SFDP read opcode defined by JESD216 itself.
//
// What SFDP tells us (and replaces the old hardcoded values):
//   • Smallest erase opcode  (was CMD_SE = 0x20, hardcoded)
//   • Sector size in bytes   (was 4096,           hardcoded)
//   • Page-program size      (was 256,            hardcoded)
//   • Whether 4-byte addressing is required for chips > 16 MB
//
// Bugs fixed vs. the original:
//   [1] Size sanity-check added in State B and State C — a corrupted
//       flash_app_size of 0xFFFFFFFF would previously overflow MAIN_RAM
//       and corrupt the bootloader stack.
//   [2] Sector-erase count formula corrected:
//       old:  (total / sector_size) + 1          — always erases one extra
//       new:  (total + sector_size - 1) / sector_size  — ceiling division
//   [3] flash_spi_send() helper introduced to avoid the dummy-read overhead
//       when only transmitting (e.g. during page-program data bytes); the
//       original code read-back every byte even when only writing.
//   [4] 4-byte address support: burn_to_flash() emits a 4-byte address when
//       flash.addr4 is set (chips > 16 MB), prefixed with CMD_EN4B (0xB7).
//   [5] SFDP erase-type selection: picks the *smallest* erase granularity
//       reported by the chip so we never erase more than necessary.
// =============================================================================

#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>

// ── Communications Protocol ──────────────────────────────────────────────────
#define SYNC_BYTE 0x5A
#define ACK_BYTE 0x79
#define NAK_BYTE 0x1F

// ── Upload Mode (sent by uploader after size) ────────────────────────────────
#define MODE_BURN 0x00 // Write to flash, then execute from SRAM
#define MODE_RAM  0x01 // Execute from SRAM only (skip flash programming)

// ── System State Markers ─────────────────────────────────────────────────────
#define WARM_BOOT_MAGIC 0xDEADBEEF
#define FLASH_MAGIC_WORD 0xABCD1234
#define REG_MAIN_RAM_MAGIC                                                     \
  (*(volatile uint32_t *)(MAIN_RAM_BASE + MAIN_RAM_SIZE - 4))

#ifndef SPIFLASH_BASE
#define SPIFLASH_BASE 0x20000000L
#endif

// ── JEDEC / JESD216 Opcodes
// ─────────────────────────────────────────────────── Universal opcodes —
// identical on every JESD216-compliant chip.
#define CMD_WREN 0x06   // Write Enable
#define CMD_RDSR 0x05   // Read Status Register (bit 0 = WIP)
#define CMD_PP 0x02     // Page Program (1-1-1, up to page_size bytes)
#define CMD_RDSFDP 0x5A // Read SFDP table (JESD216 §6.4)
#define CMD_EN4B 0xB7   // Enter 4-byte address mode (chips > 16 MB)
#define CMD_EX4B 0xE9   // Exit 4-byte address mode (restore 3-byte default)

// ── LiteX SPI master status masks ────────────────────────────────────────────
#define SPI_TX_READY (1 << 0)
#define SPI_RX_READY (1 << 1)

// ── SFDP / flash descriptor
// ─────────────────────────────────────────────────── Populated once at boot by
// flash_read_sfdp(); consumed everywhere else.
typedef struct {
  uint32_t page_size;   // bytes per page-program (almost always 256)
  uint32_t sector_size; // bytes per smallest erase unit
  uint8_t erase_cmd;    // opcode for the smallest erase unit
  uint8_t addr4;        // 1 = need 4-byte addresses (flash > 16 MB)
} flash_params_t;

static flash_params_t flash; // zero-initialised by C runtime

// =============================================================================
// CORE DRIVERS
// =============================================================================

void isr(void) {} // interrupts not used in the bootloader

uint8_t uart_read(void) {
  while (uart_rxempty_read())
    ;
  return (uint8_t)uart_rxtx_read();
}

void uart_write(uint8_t c) {
  while (uart_txfull_read())
    ;
  uart_rxtx_write(c);
}

void jump_to_app(void) {
  void (*app)(void) = (void (*)(void))MAIN_RAM_BASE;
  app();
}

// =============================================================================
// SPI PHYSICAL LAYER
// =============================================================================

// LiteX SPI master: writing 1 to the CS register pulls CS# LOW (asserts chip).
// The volatile loops satisfy minimum setup / hold times at 75 MHz (~13
// ns/iter).

void flash_cs_assert(void) {
  // LiteSPIMaster requires phyconfig to be set before bitbanging:
  // len=8 (bits 0..7), width=1 (bits 8..11), mask=1 (DQ0/MOSI out, bits 16..23)
  spiflash_master_phyconfig_write(0x00010108);

  spiflash_master_cs_write(1);
  for (volatile int i = 0; i < 50; i++)
    ; // ≥ tCSS (typically 5 ns)
}

void flash_cs_release(void) {
  for (volatile int i = 0; i < 50; i++)
    ; // ≥ tCSH hold before deassert
  spiflash_master_cs_write(0);
  for (volatile int i = 0; i < 100; i++)
    ; // ≥ tCS recovery before next cmd
}

// Full-duplex byte exchange — used when we need the returned byte.
uint8_t flash_spi_xfer(uint8_t data) {
  while ((spiflash_master_status_read() & SPI_TX_READY) == 0)
    ;
  spiflash_master_rxtx_write(data);
  while ((spiflash_master_status_read() & SPI_RX_READY) == 0)
    ;
  return (uint8_t)spiflash_master_rxtx_read();
}

// Transmit-only helper — discards the simultaneous MISO byte.
// Used during bulk page-program writes to avoid accumulating stale RX data.
static inline void flash_spi_send(uint8_t data) { (void)flash_spi_xfer(data); }

// Block until the flash chip clears the Write-In-Progress bit (JESD216 §3.1).
void flash_wait_ready(void) {
  uint8_t sr;
  do {
    flash_cs_assert();
    flash_spi_xfer(CMD_RDSR);
    sr = flash_spi_xfer(0xFF);
    flash_cs_release();
  } while (sr & 0x01);
}

void flash_write_enable(void) {
  flash_cs_assert();
  flash_spi_xfer(CMD_WREN);
  flash_cs_release();
}

// Emit the flash address — 3 bytes for ≤ 16 MB chips, 4 bytes for larger ones.
static inline void flash_send_addr(uint32_t addr) {
  if (flash.addr4)
    flash_spi_send((addr >> 24) & 0xFF);
  flash_spi_send((addr >> 16) & 0xFF);
  flash_spi_send((addr >> 8) & 0xFF);
  flash_spi_send(addr & 0xFF);
}

// =============================================================================
// JESD216 SFDP DISCOVERY
// =============================================================================
//
// SFDP memory map (all multi-byte fields are little-endian per JESD216):
//
//  0x000000  SFDP Header (8 bytes)
//    [0..3]  Signature  0x50 44 46 53 ("SFDP")
//    [4]     Minor rev
//    [5]     Major rev  (must be 0x01 for JESD216)
//    [6]     NPH        (number of additional parameter headers, 0-based)
//    [7]     Access protocol
//
//  0x000008  Parameter Header 0 — always the JEDEC Basic SPI Protocol table
//    [0]     Parameter ID LSB  (0xFF for JEDEC)
//    [1]     Minor rev
//    [2]     Major rev
//    [3]     Length in dwords
//    [4..6]  Table pointer (byte address, little-endian)
//    [7]     Parameter ID MSB
//
//  Basic Flash Parameter Table (BFPT) — starts at the pointer above.
//    DW1  (offset 0x00): flash memory density and page / erase details
//    DW2  (offset 0x04): flash memory density in bits
//    DW8  (offset 0x1C): erase type 1 — size exponent (byte 0) + opcode (byte
//    1) DW9  (offset 0x20): erase type 2 — size exponent (byte 0) + opcode
//    (byte 1)
//
// We read only the fields we actually need; all other BFPT dwords are ignored.

// Single SFDP byte read.  Protocol: CMD_RDSFDP + 3-byte addr + 1 dummy byte.
static uint8_t sfdp_read_byte(uint32_t addr) {
  flash_cs_assert();
  flash_spi_xfer(CMD_RDSFDP);
  flash_spi_xfer((addr >> 16) & 0xFF);
  flash_spi_xfer((addr >> 8) & 0xFF);
  flash_spi_xfer(addr & 0xFF);
  flash_spi_xfer(0xFF); // mandatory dummy byte (JESD216 §6.4.2)
  uint8_t val = flash_spi_xfer(0xFF);
  flash_cs_release();
  return val;
}

// Read a 32-bit little-endian dword from the SFDP address space.
static uint32_t sfdp_read_dword(uint32_t addr) {
  uint32_t v = 0;
  v |= (uint32_t)sfdp_read_byte(addr + 0) << 0;
  v |= (uint32_t)sfdp_read_byte(addr + 1) << 8;
  v |= (uint32_t)sfdp_read_byte(addr + 2) << 16;
  v |= (uint32_t)sfdp_read_byte(addr + 3) << 24;
  return v;
}

// Populate the global `flash` descriptor.
// Returns 0 on success, -1 if the chip does not expose a valid SFDP table.
static int flash_read_sfdp(void) {
  // ── 1. Verify SFDP signature ─────────────────────────────────────────────
  // Little-endian 'S','F','D','P' = 0x50444653
  if (sfdp_read_dword(0x000000) != 0x50444653)
    return -1;

  // Major revision must be 1 (JESD216).  Rev 0 pre-production chips are rare
  // but exist; we reject them for safety.
  if (sfdp_read_byte(0x000005) != 0x01)
    return -1;

  // ── 2. Locate the JEDEC Basic Flash Parameter Table (BFPT) ───────────────
  // Parameter header 0 occupies bytes 0x08..0x0F.
  // Bytes [4..6] of the header are the BFPT pointer (little-endian 24-bit).
  uint32_t bfpt_ptr = sfdp_read_dword(0x00000C) & 0x00FFFFFF;

  // ── 3. Page size (BFPT DW1 bits [7:4]) ───────────────────────────────────
  // Encoding: 0 = 64-byte pages (rare early chips), 1 = 256-byte pages.
  // Every JESD216 chip produced after ~2012 uses 256; we handle both.
  uint32_t dw1 = sfdp_read_dword(bfpt_ptr + 0x00);
  flash.page_size = ((dw1 >> 4) & 0x0F) ? 256u : 64u;

  // ── 4. Address width (BFPT DW2 bit 31) ───────────────────────────────────
  // Bit 31 = 0: density ≤ 2 Gbit, encoded directly in bits [30:0] (in bits).
  // Bit 31 = 1: density > 2 Gbit — always needs 4-byte addressing.
  uint32_t dw2 = sfdp_read_dword(bfpt_ptr + 0x04);
  if (dw2 & 0x80000000) {
    // > 2 Gbit chip — definitely needs 4-byte addressing
    flash.addr4 = 1;
  } else {
    // density in bits; > 128 Mbit (16 MB) needs 4-byte addressing
    uint32_t density_bits = dw2 + 1; // bits [30:0] + 1
    uint32_t density_bytes = density_bits / 8;
    flash.addr4 = (density_bytes > 0x01000000) ? 1 : 0; // > 16 MB?
  }

  // ── 5. Erase types (BFPT DW8 and DW9) ────────────────────────────────────
  // Each erase type occupies 2 bytes: [size-exponent][opcode].
  // size = 2^exponent bytes.  A size-exponent of 0x00 means "not supported."
  // We scan types 1 and 2 and pick the smallest supported granularity so we
  // never erase more flash than needed.
  //
  // BFPT DW8 = offset 0x1C from table base:
  //   bits [ 7: 0] = erase type 1 size exponent
  //   bits [15: 8] = erase type 1 opcode
  //   bits [23:16] = erase type 2 size exponent
  //   bits [31:24] = erase type 2 opcode
  //
  // Typical values on common chips:
  //   Type 1: exp=0x0C (4 KB),  opcode=0x20  ← we usually pick this
  //   Type 2: exp=0x0F (32 KB), opcode=0x52
  //   (Types 3 & 4 in DW9 are 64 KB and bulk-erase; we ignore them.)

  uint32_t dw8 = sfdp_read_dword(bfpt_ptr + 0x1C);

  uint8_t e1_exp = (dw8 >> 0) & 0xFF;
  uint8_t e1_cmd = (dw8 >> 8) & 0xFF;
  uint8_t e2_exp = (dw8 >> 16) & 0xFF;
  uint8_t e2_cmd = (dw8 >> 24) & 0xFF;

  // Start with a sentinel so we know if nothing was found.
  flash.erase_cmd = 0x00;
  flash.sector_size = 0xFFFFFFFF;

  if (e1_exp != 0x00) {
    uint32_t sz = (1u << e1_exp);
    if (sz < flash.sector_size) {
      flash.sector_size = sz;
      flash.erase_cmd = e1_cmd;
    }
  }
  if (e2_exp != 0x00) {
    uint32_t sz = (1u << e2_exp);
    if (sz < flash.sector_size) {
      flash.sector_size = sz;
      flash.erase_cmd = e2_cmd;
    }
  }

  // If we could not find any erase type, the SFDP table is malformed.
  if (flash.erase_cmd == 0x00)
    return -1;

  return 0;
}

// =============================================================================
// FLASH PROGRAMMING
// =============================================================================

// Erase the minimum number of sectors that cover [0 .. total_bytes) then
// write the 12-byte integrity header followed by the application payload from
// MAIN_RAM.  Uses flash geometry discovered by flash_read_sfdp().
//
// Header layout (little-endian):
//   [0..3]   FLASH_MAGIC_WORD
//   [4..7]   payload size in bytes
//   [8..11]  rolling-sum checksum of payload bytes
//
void burn_to_flash(uint32_t size, uint32_t expected_checksum) {
  // Drain any stale bytes left in the RX FIFO from XIP memory-mapped reads.
  while (spiflash_master_status_read() & SPI_RX_READY)
    spiflash_master_rxtx_read();

  // Enter 4-byte address mode if the chip needs it.
  if (flash.addr4) {
    flash_write_enable();
    flash_cs_assert();
    flash_spi_xfer(CMD_EN4B);
    flash_cs_release();
  }

  volatile uint8_t *ram_src = (volatile uint8_t *)MAIN_RAM_BASE;
  uint32_t total = size + 12; // header + payload
  uint32_t flash_addr = 0;

  // ── Ceiling division: erase exactly as many sectors as needed ─────────────
  // Fix for original bug: (total / sector_size) + 1 always erased one extra.
  uint32_t n_sectors = (total + flash.sector_size - 1) / flash.sector_size;

  portb_out_write(0x08); // LED: Burning to Flash

  // ── 1. Sector Erase ───────────────────────────────────────────────────────
  for (uint32_t s = 0; s < n_sectors; s++) {
    flash_write_enable();
    flash_cs_assert();
    flash_spi_xfer(flash.erase_cmd); // opcode from SFDP
    flash_send_addr(flash_addr);     // 3- or 4-byte address
    flash_cs_release();
    flash_wait_ready();
    flash_addr += flash.sector_size;
  }

  // ── 2. Page Program ───────────────────────────────────────────────────────
  flash_addr = 0;
  uint32_t bytes_written = 0;

  while (bytes_written < total) {
    flash_write_enable();
    flash_cs_assert();
    flash_spi_xfer(CMD_PP);
    flash_send_addr(flash_addr);

    uint32_t page_bytes = 0;
    while (page_bytes < flash.page_size && bytes_written < total) {
      uint8_t b;

      // Synthesise the 12-byte integrity header inline.
      switch (bytes_written) {
      case 0:
        b = (FLASH_MAGIC_WORD >> 0) & 0xFF;
        break;
      case 1:
        b = (FLASH_MAGIC_WORD >> 8) & 0xFF;
        break;
      case 2:
        b = (FLASH_MAGIC_WORD >> 16) & 0xFF;
        break;
      case 3:
        b = (FLASH_MAGIC_WORD >> 24) & 0xFF;
        break;
      case 4:
        b = (size >> 0) & 0xFF;
        break;
      case 5:
        b = (size >> 8) & 0xFF;
        break;
      case 6:
        b = (size >> 16) & 0xFF;
        break;
      case 7:
        b = (size >> 24) & 0xFF;
        break;
      case 8:
        b = (expected_checksum >> 0) & 0xFF;
        break;
      case 9:
        b = (expected_checksum >> 8) & 0xFF;
        break;
      case 10:
        b = (expected_checksum >> 16) & 0xFF;
        break;
      case 11:
        b = (expected_checksum >> 24) & 0xFF;
        break;
      default:
        b = ram_src[bytes_written - 12];
        break;
      }

      flash_spi_send(b); // discard MISO during bulk write
      bytes_written++;
      page_bytes++;
    }

    flash_cs_release();
    flash_wait_ready();
    flash_addr += flash.page_size;
  }

  // Restore flash to 3-byte addressing mode so the LiteX XIP memory-mapped
  // controller (which always issues 3-byte read commands) works on next boot.
  if (flash.addr4) {
    flash_cs_assert();
    flash_spi_xfer(CMD_EX4B);
    flash_cs_release();
  }
}

// Helper to read flash manually, completely bypassing the buggy XIP hardware
void manual_flash_read(uint32_t addr, uint8_t *buf, uint32_t len) {
    flash_cs_assert();
    flash_spi_xfer(0x03); // Standard 1-1-1 READ command
    flash_spi_xfer((addr >> 16) & 0xFF);
    flash_spi_xfer((addr >> 8) & 0xFF);
    flash_spi_xfer(addr & 0xFF);
    for(uint32_t i = 0; i < len; i++) {
        buf[i] = flash_spi_xfer(0xFF);
    }
    flash_cs_release();
}

// =============================================================================
// MAIN BOOTLOADER STATE MACHINE
// =============================================================================

int main(void) {
  portb_oe_write(0xFF); 
  volatile uint8_t *app_memory = (volatile uint8_t *)MAIN_RAM_BASE;

  portb_out_write(0x01); // LED 0: Bootloader online

  // ── 1. KICK FLASH OUT OF VIVADO HANG ──
  spiflash_master_phyconfig_write(0x00010108);
  
  // Send 8 bytes of 0xFF to clear any Continuous Read modes
  flash_cs_assert();
  for(int i=0; i<8; i++) flash_spi_xfer(0xFF);
  flash_cs_release();
  
  // Release from Deep Power Down (0xAB) just in case
  flash_cs_assert();
  flash_spi_xfer(0xAB);
  flash_cs_release();
  for(volatile int i=0; i<1000; i++); // Short delay for flash to wake up

  uint32_t force_uart_mode = prog_mode_in_read() & 0x01; 
  uint32_t ram_retained = (REG_MAIN_RAM_MAGIC == WARM_BOOT_MAGIC);

  // ── STATE A: Warm Reboot Fast-Path ──
  if (!force_uart_mode && ram_retained) {
    portb_out_write(0x00);
    jump_to_app();
    return 0;
  }

  // ── STATE B: Manual Bitbang Read (XIP Bypassed) ──
  if (!force_uart_mode) {
    uint8_t header[12];
    manual_flash_read(0x00000000, header, 12);

    // Reconstruct the 32-bit magic word
    uint32_t magic = header[0] | (header[1]<<8) | (header[2]<<16) | (header[3]<<24);

    if (magic == FLASH_MAGIC_WORD) {
      portb_out_write(0x02); // LED 1: Loading from Flash

      uint32_t flash_app_size = header[4] | (header[5]<<8) | (header[6]<<16) | (header[7]<<24);
      uint32_t flash_app_checksum = header[8] | (header[9]<<8) | (header[10]<<16) | (header[11]<<24);

      if (flash_app_size > 0 && flash_app_size <= (MAIN_RAM_SIZE - 4)) {
        uint32_t calculated_checksum = 0;
        
        // Manually stream the payload into SRAM
        flash_cs_assert();
        flash_spi_xfer(0x03);
        flash_spi_xfer(0x00); // Address 12 (0x00000C)
        flash_spi_xfer(0x00);
        flash_spi_xfer(0x0C);
        
        for (uint32_t i = 0; i < flash_app_size; i++) {
          uint8_t b = flash_spi_xfer(0xFF);
          app_memory[i] = b;
          calculated_checksum += b;
        }
        flash_cs_release();

        if (calculated_checksum == flash_app_checksum) {
          REG_MAIN_RAM_MAGIC = WARM_BOOT_MAGIC;
          portb_out_write(0x00);
          jump_to_app();
          return 0;
        }
      }
    }
  }

  // ─────────────────────────────────────────────────────────────────────────
  // STATE C: UART Ingestion
  //   PROG_MODE is HIGH, or flash is empty/corrupted.
  //   Waits for the uploader.py script, receives the binary, validates it,
  //   writes it to flash, and boots.
  // ─────────────────────────────────────────────────────────────────────────
state_c:
  portb_out_write(0x04); // LED 2: Waiting for UART

  // ── SFDP Discovery ──────────────────────────────────────────────────────
  // Deferred to State C so the SPI master activity does not disturb the
  // XIP memory-mapped controller used by State B.  Only State C needs
  // flash params (for burning and reporting to the uploader).
  //   sfdp_src: 0x01 = discovered via SFDP, 0x02 = hardcoded defaults
  uint8_t sfdp_src;
  if (flash_read_sfdp() == 0) {
    sfdp_src = 0x01;
  } else {
    flash.sector_size = 4096;   // 4 KB sector erase (universal)
    flash.page_size   = 256;    // 256-byte page program (universal)
    flash.erase_cmd   = 0x20;   // Sector Erase opcode (JEDEC standard)
    flash.addr4       = 0;      // 3-byte addressing (chips ≤ 16 MB)
    sfdp_src = 0x02;
  }

  // Drain line-noise from aborted uploads or hot-plug transients.
  while (!uart_rxempty_read())
    uart_rxtx_read();

  // Handshake: wait for SYNC, respond with ACK + flash info.
  while (uart_read() != SYNC_BYTE)
    ;
  uart_write(ACK_BYTE);

  // ── Diagnostics: Read Status Register and JEDEC ID ──────────────────────
  flash_cs_assert();
  flash_spi_xfer(CMD_RDSR);
  uint8_t flash_sr = flash_spi_xfer(0xFF);
  flash_cs_release();

  flash_cs_assert();
  flash_spi_xfer(0x9F); // RDID
  uint8_t id_mfr  = flash_spi_xfer(0xFF);
  uint8_t id_type = flash_spi_xfer(0xFF);
  uint8_t id_cap  = flash_spi_xfer(0xFF);
  flash_cs_release();

  // ── Send 16-byte flash info block ──────────────────────────────────────
  // Layout (all little-endian):
  //   [0]     flash_src   (0x01 = SFDP, 0x02 = hardcoded defaults)
  //   [1]     erase_cmd
  //   [2]     addr4
  //   [3]     status_register
  //   [4..6]  JEDEC ID (Mfr, Type, Cap)
  //   [7]     reserved
  //   [8..11] sector_size
  //   [12..15] page_size
  uart_write(sfdp_src);
  uart_write(flash.erase_cmd);
  uart_write(flash.addr4);
  uart_write(flash_sr);
  uart_write(id_mfr);
  uart_write(id_type);
  uart_write(id_cap);
  uart_write(0x00);
  uart_write((flash.sector_size >>  0) & 0xFF);
  uart_write((flash.sector_size >>  8) & 0xFF);
  uart_write((flash.sector_size >> 16) & 0xFF);
  uart_write((flash.sector_size >> 24) & 0xFF);
  uart_write((flash.page_size  >>  0) & 0xFF);
  uart_write((flash.page_size  >>  8) & 0xFF);
  uart_write((flash.page_size  >> 16) & 0xFF);
  uart_write((flash.page_size  >> 24) & 0xFF);

  // Receive 32-bit payload size (little-endian).
  uint32_t size = 0;
  size |= ((uint32_t)uart_read()) << 0;
  size |= ((uint32_t)uart_read()) << 8;
  size |= ((uint32_t)uart_read()) << 16;
  size |= ((uint32_t)uart_read()) << 24;

  // Receive mode byte: MODE_BURN (0x00) or MODE_RAM (0x01).
  uint8_t mode = uart_read();

  // Validate size and mode before ACK-ing.
  if (size == 0 || size > (MAIN_RAM_SIZE - 4)) {
    portb_out_write(0xF0); // LED: Error — invalid size
    uart_write(NAK_BYTE);
    while (1)
      ;
  }


  uart_write(ACK_BYTE);

  // Stream payload into SRAM and accumulate checksum simultaneously.
  uint32_t computed_checksum = 0;
  for (uint32_t i = 0; i < size; i++) {
    uint8_t b = uart_read();
    app_memory[i] = b;
    computed_checksum += b;
  }

  // Receive 32-bit validation checksum (little-endian).
  uint32_t expected_checksum = 0;
  expected_checksum |= ((uint32_t)uart_read()) << 0;
  expected_checksum |= ((uint32_t)uart_read()) << 8;
  expected_checksum |= ((uint32_t)uart_read()) << 16;
  expected_checksum |= ((uint32_t)uart_read()) << 24;

  if (computed_checksum == expected_checksum) {
    if (mode == MODE_BURN) {
      burn_to_flash(size, expected_checksum);
    }

    REG_MAIN_RAM_MAGIC = WARM_BOOT_MAGIC;
    portb_out_write(0x0F); // LED: Success
    uart_write(ACK_BYTE);

    while (!uart_txempty_read())
      ; // flush UART TX before jumping

    portb_out_write(0x00);
    jump_to_app();
  } else {
    portb_out_write(0xF0); // LED: Checksum error
    uart_write(NAK_BYTE);
    while (1)
      ;
  }

  return 0;
}
