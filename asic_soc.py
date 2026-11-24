import os
import struct
import argparse
from migen import *
from litex.soc.integration.soc_core import SoCCore, soc_core_args, soc_core_argdict
from litex.soc.integration.builder  import Builder, builder_args, builder_argdict
from litex.soc.integration.soc import SoCRegion
from litex.soc.cores.uart import UARTPHY, UART
from litex.build.generic_platform import GenericPlatform, Pins, Subsignal

# LiteSPI and LiteI2C imports
from litei2c import LiteI2C
from litespi import LiteSPI
from litespi.phy.generic import LiteSPIPHY
from litespi.modules import S25FL032
from litespi.opcodes import SpiNorFlashOpCodes as Codes

from litex.soc.cores.gpio import GPIOOut, GPIOIn, GPIOTristate
from timer1_core import Timer1LiteX
from watchdog    import WatchdogTimer

# -----------------------------------------------------------------------------
# 1. Generic ASIC Platform & I/O Definition
# -----------------------------------------------------------------------------
_io = [
    # Basic System
    ("clk", 0, Pins(1)),
    ("rst", 0, Pins(1)),
    
    # Dedicated Programming/Boot Mode Pin
    ("prog_mode", 0, Pins(1)),
    
    # UART0 (Main Serial)
    ("serial", 0,
        Subsignal("tx", Pins(1)),
        Subsignal("rx", Pins(1)),
    ),
    
    # UART1
    ("uart1", 0,
        Subsignal("tx", Pins(1)),
        Subsignal("rx", Pins(1)),
    ),

    # PWM Channels
    ("pwm", 0,
        Subsignal("ch_a", Pins(1)),
        Subsignal("ch_b", Pins(1)),
        Subsignal("ch_c", Pins(1)),
        Subsignal("ch_d", Pins(1)),
    ),

    # SPI
    ("spi", 0,
        Subsignal("clk",  Pins(1)),
        Subsignal("cs_n", Pins(1)),
        Subsignal("mosi", Pins(1)),
        Subsignal("miso", Pins(1)),
    ),

    # I2C
    ("i2c", 0,
        Subsignal("scl_i", Pins(1)),
        Subsignal("scl_o", Pins(1)),
        Subsignal("scl_oe", Pins(1)),
        Subsignal("sda_i", Pins(1)),
        Subsignal("sda_o", Pins(1)),
        Subsignal("sda_oe", Pins(1)),
    ),

    # Dedicated SPI Flash
    ("spiflash", 0,
        Subsignal("cs_n", Pins(1)),
        Subsignal("clk",  Pins(1)),
        Subsignal("mosi", Pins(1)),
        Subsignal("miso", Pins(1)),
    ),

    # GPIO Ports (Replacing specific LEDs/Switches/Buttons)
    ("portb", 0, 
        Subsignal("i", Pins(8)),
        Subsignal("o", Pins(8)),
        Subsignal("oe", Pins(8)),
    ), 
    ("portc", 0, 
        Subsignal("i", Pins(8)),
        Subsignal("o", Pins(8)),
        Subsignal("oe", Pins(8)),
    ), 
    ("portd", 0, 
        Subsignal("i", Pins(8)),
        Subsignal("o", Pins(8)),
        Subsignal("oe", Pins(8)),
    ), 
]

class ASICPlatform(GenericPlatform):
    def __init__(self):
        GenericPlatform.__init__(self, "ASIC", _io)

    # Override the build method to just dump the Verilog file
    def build(self, fragment, build_dir="build", build_name="asic_top", **kwargs):
        # Create the build directory (e.g., build/gateware)
        os.makedirs(build_dir, exist_ok=True)
        
        # Aggressively pop LiteX builder arguments that Migen doesn't understand
        kwargs.pop("run", None)
        kwargs.pop("build_backend", None)
        kwargs.pop("builder", None)
        kwargs.pop("synth_opts", None)
        
        # Generate the Verilog netlist
        v_output = self.get_verilog(fragment, **kwargs)
        
        # Write the flat Verilog to the target file
        v_file = os.path.join(build_dir, f"{build_name}.v")
        v_output.write(v_file)
        
        print(f"\n[ASIC Platform] Success! Verilog generated at: {v_file}")
        return v_output

# -----------------------------------------------------------------------------
# 2. ASIC Clock and Reset Generator (CRG)
# -----------------------------------------------------------------------------
class ASICCRG(Module):
    def __init__(self, platform):
        self.rst = Signal()
        self.clock_domains.cd_sys = ClockDomain()

        # Route the generic clk/rst pins directly to the system clock domain
        self.comb += self.cd_sys.clk.eq(platform.request("clk"))
        self.comb += self.cd_sys.rst.eq(platform.request("rst") | self.rst)

# -----------------------------------------------------------------------------
# 3. The Custom ASIC SoC
# -----------------------------------------------------------------------------
class CustomASICSoC(SoCCore):
    def __init__(self, sys_clk_freq=int(100e6), **kwargs):
        platform = ASICPlatform()
        
        # --- Boot ROM Injection (Kept exactly as requested) ---
        rom_init = []
        firmware_path = "firmware/bootloader.bin"
        if os.path.exists(firmware_path):
            with open(firmware_path, "rb") as f:
                data = f.read()
            data += b'\x00' * ((4 - len(data) % 4) % 4)
            for i in range(0, len(data), 4):
                rom_init.append(struct.unpack("<I", data[i:i+4])[0])
            print(f"SUCCESS: Found bootloader.bin. Injecting {len(rom_init)} words into ROM.")
            
        kwargs["cpu_type"] = "picorv32"
        kwargs["cpu_variant"] = "standard"
        kwargs["uart_name"] = "serial"
        kwargs["uart_rx_fifo_rx_we"] = True
        
        # Memory map
        kwargs["integrated_rom_size"]  = 4 * 1024
        kwargs["integrated_rom_init"]  = rom_init
        
        # NOTE: For a real ASIC tapeout, SRAMs shouldn't be synthesized as logic. 
        # You will eventually set these to 0 and attach a physical memory compiler macro.
        # Keeping them here for now so your code compiles properly.
        kwargs["integrated_sram_size"] = 32  * 1024
        kwargs["integrated_main_ram_size"] = 32 * 1024
        kwargs["with_led_chaser"] = False
        
        # Initialize SoCCore instead of BaseSoC
        SoCCore.__init__(self, platform, sys_clk_freq, ident="ATmega328P ASIC Clone", **kwargs)
        
        # Attach the ASIC CRG
        self.submodules.crg = ASICCRG(platform)
        
        self.add_constant("BIOS_NONE", 1)
        _here = os.path.dirname(os.path.abspath(__file__))
        
        # --- GPIO Modules ---
        # Mapped to generic ports instead of physical buttons/leds
        self.submodules.portb = GPIOTristate(self.platform.request("portb"))
        self.submodules.portc = GPIOTristate(self.platform.request("portc"))
        self.submodules.portd = GPIOTristate(self.platform.request("portd"))
        
        # --- Dedicated Programming Mode Input ---
        self.submodules.prog_mode = GPIOIn(self.platform.request("prog_mode"))
        
        # --- 3b. PWM Timer (timer1.v via AutoCSR) ---
        self.platform.add_source(os.path.join(_here, "timer1.v"), language="verilog")
        pwm_pads = self.platform.request("pwm")
        self.submodules.timer1 = Timer1LiteX(pads=pwm_pads)
        self.irq.add("timer1", use_loc_if_exists=True)
        
        # --- Watchdog ---
        self.submodules.watchdog = WatchdogTimer()
        self.comb += If(self.watchdog.reset_out, self.cpu.reset.eq(1))
        
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
        
        # Retained dedicated Flash SPI (Mapped to generic ASIC pads)
        self.add_spi_flash(
            mode="1x",
            module=S25FL032(Codes.READ_1_1_1),
            with_master=True
        )
        
        # --- UART1 ---
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

    def finalize(self, *args, **kwargs):
        super().finalize(*args, **kwargs)
        print("\n=== CSR Address Map ===")
        for name, region in sorted(self.csr_regions.items()):
            print(f"  {name:<32s}  0x{region.origin:08x}")
        print(f"  {'timer1 (AutoCSR)':<32s}  (see CSR map above)")
        print(f"  {'watchdog (AutoCSR)':<32s}  (see CSR map above)")
        print("========================================\n")

def main():
    parser = argparse.ArgumentParser(description="ATmega328P Complete ASIC SoC")
    soc_core_args(parser)
    builder_args(parser)
    # Give the ASIC a target frequency for UART/Timer baud calculations
    parser.add_argument("--sys-clk-freq", default=100e6, type=float, help="System clock frequency (default: 50MHz)")
    parser.add_argument("--build", action="store_true")
    args = parser.parse_args()
    
    soc = CustomASICSoC(
        sys_clk_freq=int(args.sys_clk_freq),
        **soc_core_argdict(args)
    )
    soc.add_constant("ROM_BOOT_ADDRESS", 0x00000000)
    soc.add_constant("CPU_RESET_ADDRESS", 0x00000000)
    
    builder_kwargs = builder_argdict(args)
    builder_kwargs["compile_software"] = False
    
    builder = Builder(soc, **builder_kwargs)
    builder.build(run=args.build, build_name = "asic_top", regular_comb = False, hierarchical = False)

if __name__ == "__main__":
    main()
