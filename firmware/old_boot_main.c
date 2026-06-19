#include <generated/csr.h>
#include <generated/mem.h>
#include <generated/soc.h>
#include <stdint.h>

// --- Communications Protocol ---
#define SYNC_BYTE 0x5A
#define ACK_BYTE  0x79
#define NAK_BYTE  0x1F 

// --- System State Markers ---
#define WARM_BOOT_MAGIC  0xDEADBEEF
#define FLASH_MAGIC_WORD 0xABCD1234
#define REG_MAIN_RAM_MAGIC (*(volatile uint32_t *)(MAIN_RAM_BASE + MAIN_RAM_SIZE - 4))

// Provide fallback in case LiteX headers haven't regenerated
#ifndef SPIFLASH_BASE
#define SPIFLASH_BASE 0x20000000L
#endif

// --- Standard SPI Flash Opcodes ---
#define CMD_WREN  0x06  // Write Enable (Unlocks flash for programming)
#define CMD_SE    0x20  // Sector Erase (Sets a 4KB block to 0xFF)
#define CMD_PP    0x02  // Page Program (Writes up to 256 bytes of data)
#define CMD_RDSR  0x05  // Read Status Register

// --- Hardware Abstraction Masks ---
#define SPIFLASH_TX_READY_MASK (1 << 0) 
#define SPIFLASH_RX_READY_MASK (1 << 1) 

// ============================================================================
// CORE DRIVERS
// ============================================================================
//
void isr(void) {}

uint8_t uart_read(void) {
  while (uart_rxempty_read());
  return (uint8_t)uart_rxtx_read();
}

void uart_write(uint8_t c) {
  while (uart_txfull_read());
  uart_rxtx_write(c);
}

void jump_to_app(void) {
  void (*app)(void) = (void (*)(void))MAIN_RAM_BASE;
  app();
}

// ============================================================================
// SPI PHYSICAL LAYER (PHY) DRIVERS
// ============================================================================

// Safely pull Chip Select LOW. The 'volatile' loop forces the 75MHz CPU 
// to wait ~50ns to meet the electrical Setup Time required by the flash chip.
void flash_cs_assert(void) {
  spiflash_master_cs_write(1);
  for(volatile int i=0; i<50; i++); 
}

// Safely push Chip Select HIGH. The 'volatile' loop forces the CPU 
// to wait ~100ns to meet the CS High Recovery Time before the next command.
void flash_cs_release(void) {
  for(volatile int i=0; i<50; i++); 
  spiflash_master_cs_write(0);
  for(volatile int i=0; i<100; i++); 
}

// Full-Duplex synchronous transfer. Guarantees the hardware FIFOs stay 
// balanced so the shift register never desynchronizes.
uint8_t flash_spi_transfer(uint8_t data) {
  while ((spiflash_master_status_read() & SPIFLASH_TX_READY_MASK) == 0);
  spiflash_master_rxtx_write(data);

  while ((spiflash_master_status_read() & SPIFLASH_RX_READY_MASK) == 0);
  return (uint8_t)spiflash_master_rxtx_read();
}

// Blocks CPU execution until the flash chip finishes a physical Erase or Write.
void flash_wait_until_ready(void) {
  uint8_t status;
  do {
    flash_cs_assert();
    flash_spi_transfer(CMD_RDSR);
    status = flash_spi_transfer(0xFF); 
    flash_cs_release();
  } while (status & 0x01); // Bit 0 is the Write-In-Progress (WIP) flag
}

// Unlocks the flash chip's write-protection circuitry.
void flash_write_enable(void) {
  flash_cs_assert();
  flash_spi_transfer(CMD_WREN);
  flash_cs_release();
}

// ============================================================================
// FLASH PROGRAMMING ROUTINE
// ============================================================================

// Writes a 12-byte integrity header + the payload into permanent flash.
void burn_to_flash(uint32_t size, uint32_t expected_checksum) {
  // Purge any ghost data left in the RX FIFO from previous memory-mapped reads
  while ((spiflash_master_status_read() & SPIFLASH_RX_READY_MASK) != 0) {
    spiflash_master_rxtx_read(); 
  }

  volatile uint8_t *ram_source = (volatile uint8_t *)MAIN_RAM_BASE;
  uint32_t current_flash_addr = 0;

  // Header is now 12 bytes: [MAGIC (4)] [SIZE (4)] [CHECKSUM (4)]
  uint32_t total_bytes_to_write = size + 12;
  uint32_t sectors_to_erase = (total_bytes_to_write / 4096) + 1;
  
  portb_out_write(0x08); // LED: Burning to Flash

  // 1. Sector Erase Sequence (Clears blocks to 0xFF)
  for (uint32_t s = 0; s < sectors_to_erase; s++) {
    flash_write_enable();
    flash_cs_assert();
    flash_spi_transfer(CMD_SE);
    flash_spi_transfer((current_flash_addr >> 16) & 0xFF);
    flash_spi_transfer((current_flash_addr >> 8) & 0xFF);
    flash_spi_transfer(current_flash_addr & 0xFF);
    flash_cs_release();
    
    flash_wait_until_ready();
    current_flash_addr += 4096;
  }

  // 2. Page Program Sequence (Writes in 256-byte chunks)
  current_flash_addr = 0;
  uint32_t bytes_written = 0;

  while (bytes_written < total_bytes_to_write) {
    flash_write_enable();
    flash_cs_assert();
    flash_spi_transfer(CMD_PP);
    flash_spi_transfer((current_flash_addr >> 16) & 0xFF);
    flash_spi_transfer((current_flash_addr >> 8) & 0xFF);
    flash_spi_transfer(current_flash_addr & 0xFF);

    uint32_t page_bytes = 0;
    while (page_bytes < 256 && bytes_written < total_bytes_to_write) {
      uint8_t byte_to_send;
      
      // Inline synthesis of the 12-byte header
      if (bytes_written == 0)       byte_to_send = (FLASH_MAGIC_WORD >> 0) & 0xFF;
      else if (bytes_written == 1)  byte_to_send = (FLASH_MAGIC_WORD >> 8) & 0xFF;
      else if (bytes_written == 2)  byte_to_send = (FLASH_MAGIC_WORD >> 16) & 0xFF;
      else if (bytes_written == 3)  byte_to_send = (FLASH_MAGIC_WORD >> 24) & 0xFF;
      
      else if (bytes_written == 4)  byte_to_send = (size >> 0) & 0xFF;
      else if (bytes_written == 5)  byte_to_send = (size >> 8) & 0xFF;
      else if (bytes_written == 6)  byte_to_send = (size >> 16) & 0xFF;
      else if (bytes_written == 7)  byte_to_send = (size >> 24) & 0xFF;
      
      else if (bytes_written == 8)  byte_to_send = (expected_checksum >> 0) & 0xFF;
      else if (bytes_written == 9)  byte_to_send = (expected_checksum >> 8) & 0xFF;
      else if (bytes_written == 10) byte_to_send = (expected_checksum >> 16) & 0xFF;
      else if (bytes_written == 11) byte_to_send = (expected_checksum >> 24) & 0xFF;
      
      // Once header is sent, stream the actual application code from SRAM
      else {
        byte_to_send = ram_source[bytes_written - 12];
      }
      
      flash_spi_transfer(byte_to_send);
      bytes_written++;
      page_bytes++;
    }
    flash_cs_release(); 
    flash_wait_until_ready();
    current_flash_addr += 256;
  }
}

// ============================================================================
// MAIN BOOTLOADER STATE MACHINE
// ============================================================================

int main(void) {
  volatile uint8_t *app_memory = (volatile uint8_t *)MAIN_RAM_BASE;

  portb_out_write(0x01); // LED 0: Bootloader Online

  uint32_t force_uart_mode = portc_in_read() & 0x01; // SW0 Switch
  uint32_t ram_retained = (REG_MAIN_RAM_MAGIC == WARM_BOOT_MAGIC);

  // ------------------------------------------------------------------
  // STATE A: Warm Reboot Fast-Path (SRAM is valid, SW0 is DOWN)
  // ------------------------------------------------------------------
  if (!force_uart_mode && ram_retained) {
    portb_out_write(0x00);
    jump_to_app();
    return 0;
  }

  // ------------------------------------------------------------------
  // STATE B: Cold Boot / Flash Shadowing (SRAM empty, SW0 is DOWN)
  // ------------------------------------------------------------------
  if (!force_uart_mode) {
    volatile uint32_t *flash_data = (volatile uint32_t *)SPIFLASH_BASE;
    
    // Check if the Flash contains our permanent firmware marker
    if (flash_data[0] == FLASH_MAGIC_WORD) {
      portb_out_write(0x02); // LED 1: Loading from Flash
      
      uint32_t flash_app_size = flash_data[1];
      uint32_t flash_app_checksum = flash_data[2];
      volatile uint8_t *flash_payload = (volatile uint8_t *)(SPIFLASH_BASE + 12);

      uint32_t calculated_checksum = 0;

      // Copy from Flash to fast SRAM, calculating integrity on the fly
      for (uint32_t i = 0; i < flash_app_size; i++) {
        uint8_t b = flash_payload[i];
        app_memory[i] = b;
        calculated_checksum += b;
      }

      // Bit-rot Protection: Only boot if the payload is perfectly intact
      if (calculated_checksum == flash_app_checksum) {
          REG_MAIN_RAM_MAGIC = WARM_BOOT_MAGIC; 
          portb_out_write(0x00);
          jump_to_app();
          return 0;
      }
      
      // If checksum fails, flash is corrupted. Fall through to State C.
    }
  }

  // ------------------------------------------------------------------
  // STATE C: UART Ingestion & Programming (SW0 is UP, or Flash Corrupted)
  // ------------------------------------------------------------------
  portb_out_write(0x04); // LED 2: Waiting for UART

  // Purge lingering line noise from aborted scripts or disconnected cables
  while (!uart_rxempty_read()) {
    uart_rxtx_read();
  }

  // Wait for the python script to initiate handshake
  while (uart_read() != SYNC_BYTE);
  uart_write(ACK_BYTE);

  // Read the 32-bit file size
  uint32_t size = 0;
  size |= ((uint32_t)uart_read()) << 0;
  size |= ((uint32_t)uart_read()) << 8;
  size |= ((uint32_t)uart_read()) << 16;
  size |= ((uint32_t)uart_read()) << 24;
  uart_write(ACK_BYTE);

  // Stream raw payload directly into SRAM at maximum hardware speed
  uint32_t computed_checksum = 0;
  for (uint32_t i = 0; i < size; i++) {
    uint8_t b = uart_read();
    app_memory[i] = b;
    computed_checksum += b; 
  }

  // Read the 32-bit validation checksum sent by python
  uint32_t expected_checksum = 0;
  expected_checksum |= ((uint32_t)uart_read()) << 0;
  expected_checksum |= ((uint32_t)uart_read()) << 8;
  expected_checksum |= ((uint32_t)uart_read()) << 16;
  expected_checksum |= ((uint32_t)uart_read()) << 24;

  // Validate and burn
  if (computed_checksum == expected_checksum) {
    // Write to permanent storage
    burn_to_flash(size, expected_checksum); 

    // Lock RAM and finalize
    REG_MAIN_RAM_MAGIC = WARM_BOOT_MAGIC;
    portb_out_write(0x0F); // LED: Success 
    uart_write(ACK_BYTE);

    // Wait until the hardware UART finishes transmitting the ACK bit
    while (!uart_txempty_read()); 

    portb_out_write(0x00);
    jump_to_app();
  } else {
    // If stream was corrupted, halt safely and show error
    portb_out_write(0xF0); // LED: Error
    uart_write(NAK_BYTE);
    while (1);
  }

  return 0;
}
