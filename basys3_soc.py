import os
import struct
import argparse
from migen import *
from litex.soc.integration.soc_core import soc_core_args, soc_core_argdict
from litex.soc.integration.builder  import Builder, builder_args, builder_argdict
from litex.soc.integration.soc import SoCRegion
from litex_boards.targets.digilent_basys3 import BaseSoC
from litex.soc.cores.uart import UARTPHY, UART
from litex.build.generic_platform import *

# New LiteSPI and LiteI2C imports
from litei2c import LiteI2C
from litespi import LiteSPI
from litespi.phy.generic import LiteSPIPHY
from litespi.modules import S25FL032
from litespi.opcodes import SpiNorFlashOpCodes as Codes

from litex.soc.cores.gpio import GPIOOut, GPIOIn, GPIOTristate
from timer1_core import Timer1LiteX
from watchdog    import WatchdogTimer

class CustomBasys3SoC(BaseSoC):
    def __init__(self, **kwargs):
        rom_init = []
        firmware_path = "firmware/bootloader.bin"
        if os.path.exists(firmware_path):
            with open(firmware_path, "rb") as f:
                data = f.read()
            data += b'\x00' * ((4 - len(data) % 4) % 4)
            for i in range(0, len(data), 4):
                rom_init.append(struct.unpack("<I", data[i:i+4])[0])
            print(f"SUCCESS: Found demo.bin. Injecting {len(rom_init)} words into ROM.")
            
        kwargs["cpu_type"] = "picorv32"
        kwargs["cpu_variant"] = "standard"
        kwargs["uart_name"] = "serial"
        kwargs["uart_rx_fifo_rx_we"] = True
        kwargs["integrated_rom_size"]  = 4 * 1024
        kwargs["integrated_rom_init"] = rom_init
        kwargs["integrated_sram_size"] = 8  * 1024
        kwargs["integrated_main_ram_size"] = 32 * 1024
        kwargs["with_led_chaser"] = False
        
        super().__init__(**kwargs)
        self.add_constant("BIOS_NONE", 1)
        _here = os.path.dirname(os.path.abspath(__file__))
        
        portb_pads = Signal(8)
        portc_pads = Signal(8)
        self.submodules.portb = GPIOOut(portb_pads)
        self.submodules.portc = GPIOIn(portc_pads)
        btns = Cat([self.platform.request(f"user_btn{i}", 0) for i in ('u', 'd', 'l', 'r')])
        self.submodules.portd = GPIOIn(btns)
        
        # ── 3b. PWM Timer (timer1.v via AutoCSR) ──
        _pwm_io = [
            ("pwm", 0,
                Subsignal("ch_a", Pins("A15")),
                Subsignal("ch_b", Pins("A17")),
                Subsignal("ch_c", Pins("C15")),
                Subsignal("ch_d", Pins("C16")),
                IOStandard("LVCMOS33")
            )
        ]
        self.platform.add_extension(_pwm_io)
        self.platform.add_source(os.path.join(_here, "timer1.v"), language="verilog")
        pwm_pads = self.platform.request("pwm")
        self.submodules.timer1 = Timer1LiteX(pads=pwm_pads)
        self.irq.add("timer1", use_loc_if_exists=True)
        
        self.submodules.watchdog = WatchdogTimer()
        self.comb += If(self.watchdog.reset_out, self.cpu.reset.eq(1))
        
        _uart1_io = [
            ("uart1", 0,
                Subsignal("tx", Pins("A14")),
                Subsignal("rx", Pins("A16")),
                IOStandard("LVCMOS33")
            )
        ]
        self.platform.add_extension(_uart1_io)
        
        _io = [
            ("spi", 0,
                Subsignal("clk",  Pins("J1")),
                Subsignal("cs_n", Pins("L2")),
                Subsignal("mosi", Pins("J2")),
                Subsignal("miso", Pins("G2")),
                IOStandard("LVCMOS33")
            ),
            ("i2c", 0,
                Subsignal("scl", Pins("H1")),
                Subsignal("sda", Pins("K2")),
                IOStandard("LVCMOS33")
            )
        ]
        self.platform.add_extension(_io)
        
        # --- Upgraded LiteI2C ---
        self.submodules.i2c = LiteI2C(
            pads         = self.platform.request("i2c"),
            sys_clk_freq = self.sys_clk_freq,
            with_master  = True,
            with_irq     = True
        )
        self.irq.add("i2c", use_loc_if_exists=True)

        # --- Upgraded LiteSPI (Generic PHY and Core) ---
        spi_pads = self.platform.request("spi")
        self.submodules.spi_phy = LiteSPIPHY(
            pads            = spi_pads,
            flash           = None,  
            device          = self.platform.device,
            default_divisor = int(self.sys_clk_freq / 1e6) 
        )
        self.submodules.spi = LiteSPI(
            phy             = self.spi_phy,
            mmap_endianness = self.cpu.endianness,
            with_master     = True,
            master_with_irq = True
        )
        self.irq.add("spi", use_loc_if_exists=True)
        
        # Retained dedicated Flash SPI
        self.add_spi_flash(
            mode="1x",
            module=S25FL032(Codes.READ_1_1_1),
            with_master=True
        )
        
        self.submodules.uart1_phy = UARTPHY(
            self.platform.request("uart1"),
            clk_freq=self.sys_clk_freq,
            baudrate=115200
        )
        self.submodules.uart1 = UART(
            self.uart1_phy,
            tx_fifo_depth=16,
            rx_fifo_depth=16,
            rx_fifo_rx_we=True
        )
        self.irq.add("uart1", use_loc_if_exists=True)
        
        leds = Cat([self.platform.request("user_led", i) for i in range(8)])
        switches = Cat([self.platform.request("user_sw", i) for i in range(8)])
        
        self.comb += [
            leds.eq(portb_pads),
            portc_pads.eq(switches)
        ]

    def finalize(self, *args, **kwargs):
        super().finalize(*args, **kwargs)
        print("\n=== ATmega328P Clone CSR Address Map ===")
        for name, region in sorted(self.csr_regions.items()):
            print(f"  {name:<32s}  0x{region.origin:08x}")
        print(f"  {'timer1 (AutoCSR)':<32s}  (see CSR map above)")
        print(f"  {'watchdog (AutoCSR)':<32s}  (see CSR map above)")
        print("========================================\n")

def main():
    parser = argparse.ArgumentParser(description="SoC")
    soc_core_args(parser)
    builder_args(parser)
    parser.add_argument("--build", action="store_true")
    args = parser.parse_args()
    
    soc = CustomBasys3SoC(**soc_core_argdict(args))
    soc.add_constant("ROM_BOOT_ADDRESS", 0x00000000)
    soc.add_constant("CPU_RESET_ADDRESS", 0x00000000)
    
    builder_kwargs = builder_argdict(args)
    builder_kwargs["compile_software"] = False
    
    builder = Builder(soc, **builder_kwargs)
    builder.build(run=args.build)

if __name__ == "__main__":
    main()