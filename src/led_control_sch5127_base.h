/////////////////////////////////////////////////////////////////////////////
/// @file led_hpex485.h
///
/// base class for LED control over systems using the SCH5127 chipset
///
/// -------------------------------------------------------------------------
///
/// Copyright (c) 2009-2010 Chris Byrne
/// 
/// This software is provided 'as-is', without any express or implied
/// warranty. In no event will the authors be held liable for any damages
/// arising from the use of this software.
/// 
/// Permission is granted to anyone to use this software for any purpose,
/// including commercial applications, and to alter it and redistribute it
/// freely, subject to the following restrictions:
/// 
/// 1. The origin of this software must not be misrepresented; you must not
/// claim that you wrote the original software. If you use this software
/// in a product, an acknowledgment in the product documentation would be
/// appreciated but is not required.
/// 
/// 2. Altered source versions must be plainly marked as such, and must not
/// be misrepresented as being the original software.
/// 
/// 3. This notice may not be removed or altered from any source
/// distribution.
///
/////////////////////////////////////////////////////////////////////////////
// Altered version, 2026: validated discovery and owned, injectable port access.
#pragma once
#include "led_control_base.h"
#include "mediasmartserverd.h"
#include "port_io.h"
#include <algorithm>
#include <cassert>
#include <iostream>

class LedControlSCH5127Base : public LedControlBase {
public:
    explicit LedControlSCH5127Base(PortIo& io = NativePortIo::instance()) : io_(io) {}
    ~LedControlSCH5127Base() override {
        for (unsigned i = 0; i < 65536;) {
            if (!permissions_[i]) { ++i; continue; }
            const unsigned start = i;
            while (i < 65536 && permissions_[i]) ++i;
            try { io_.permission(start, i - start, false); } catch (...) {}
        }
    }
    bool Init() override { return initPciLpc_() && initSch5127_(); }
    void DisableWatchdog() override {
        // Explicit opt-in only; preserve the watchdog during ordinary startup.
        IoGrant grant(io_, io_sch5127_regs_ + REG_WDT_TIME_OUT, 4);
        for (unsigned i = REG_WDT_TIME_OUT; i <= REG_WDT_CTRL; ++i)
            outb(0, io_sch5127_regs_ + i);
    }
protected:
	enum {
		GPIO_USE_SEL		= 0x00,	///< GPIO Use Select
		GP_IO_SEL			= 0x04,	///< GPIO Input/Output Select
		//					= 0x08, ///< reserved
		GP_LVL				= 0x0C,	///< GPIO Level for Input or Output
		//					= 0x10, ///< reserved
		//					= 0x14, ///< reserved
		GPO_BLINK			= 0x18,	///< GPIO Blink Enable
		// GP_SER_BLINK		= 0x1C,	///< GP Serial Blink [31:0]
		// GP_SB_CMDSTS		= 0x20,	///< GP Serial Blink Command Status [31:0]
		// GP_SB_DATA		= 0x24,	///< GP Serial BLink Data [31:0]
		//					= 0x28, ///< reserved
		// GPI_INV			= 0x2C,	///< GPIO Signal Invert
		GPIO_USE_SEL2		= 0x30, ///< GPIO Use Select 2 [60:32]
		GP_IO_SEL2			= 0x34,	///< GPIO Input/Output Select 2 [60:32]
		GP_LVL2				= 0x38,	///< GPIO Level for INput or Output 2 [60:32]
		//					= 0x3C,	///< reserved
	};
	
	/// SCH5127 Runtime Registers
	enum {
		REG_GP1				= 0x4B,	///< General Purpose I/O Data Register 1
		REG_GP2				= 0x4C,	///< General Purpose I/O Data Register 2
		REG_GP3				= 0x4D,	///< General Purpose I/O Data Register 3
		REG_GP4				= 0x4E,	///< General Purpose I/O Data Register 4
		REG_GP5				= 0x4F,	///< General Purpose I/O Data Register 5
		REG_GP6				= 0x50,	///< General Purpose I/O Data Register 6
		
		REG_WDT_TIME_OUT	= 0x65,	///< Watch-dog Timeout
		REG_WDT_VAL			= 0x66,	///< Watch-dog Timer Time-out Value
		REG_WDT_CFG			= 0x67,	///< Watch-dog timer Configuration
		REG_WDT_CTRL		= 0x68,	///< Watch-dog timer Control
		
		REG_HWM_INDEX		= 0x70,	///< HWM Index Register (SCH5127 runtime register)
		REG_HWM_DATA		= 0x71,	///< HWM Data Register (SCH5127 runtime register)
	};
	
	/// SCH5127 Hardware monitoring register set
	enum {
		HWM_PWM3_DUTY_CYCLE	= 0x32,	///< PWM3 Current Duty Cycle
	};
	

    virtual bool chkPciDeviceVendorId_(unsigned did_vid) const = 0;
    bool initPciLpc_() {
        IoGrant address(io_, 0xcf8, 4), data(io_, 0xcfc, 4);
        outl(0x8000f800, 0xcf8);
        if (!chkPciDeviceVendorId_(inl(0xcfc))) return false;
        outl(0x8000f848, 0xcf8);
        const uint32_t base = inl(0xcfc);
        if ((base & 0xffff007fU) != 1 || (base & ~1U) == 0) return false;
        io_lpc_gpiobase_ = base & ~1U;
        return true;
    }
    bool initSch5127_() {
        // Datasheet DS00002081A: global ID 0x86, LDN 0x0a runtime block,
        // 128-byte alignment within the 12-bit address space.
        for (unsigned address : {0x2eU, 0x4eU}) {
            IoGrant grant(io_, address, 2);
            struct ConfigExit {
                PortIo& io; unsigned address;
                ~ConfigExit() { try { io.write8(0xaa, address); } catch (...) {} }
            } exit{io_, address};
            outb(0x55, address);
            outb(0x20, address);
            if (inb(address + 1) != 0x86) continue;
            outb(0x07, address); outb(0x0a, address + 1);
            outb(0x60, address); const unsigned high = inb(address + 1);
            outb(0x61, address); const unsigned low = inb(address + 1);
            const unsigned base = (high << 8) | low;
            if (base < 0x100 || base > 0xf00 || (base & 0x7f)) return false;
            if (base < io_lpc_gpiobase_ + 0x40 && io_lpc_gpiobase_ < base + 0x80)
                return false;
            io_sch5127_regs_ = base;
            return true;
        }
        return false;
    }
    static void setBit32_(int bit, uint32_t& first, uint32_t& second) {
        if (bit < 0 || bit > 60) throw std::out_of_range("GPIO index");
        (bit < 32 ? first : second) |= uint32_t{1} << (bit % 32);
    }
    void doBits_(uint32_t mask, unsigned port, bool state) {
        const auto old = inl(port);
        const auto value = state ? old | mask : old & ~mask;
        if (old != value) outl(value, port);
    }
    void setGpLpcLvl_(int bit, bool state) {
        if (bit < 0 || bit > 60) throw std::out_of_range("GPIO index");
        doBits_(uint32_t{1} << (bit % 32),
                io_lpc_gpiobase_ + (bit < 32 ? GP_LVL : GP_LVL2), state);
    }
    void setGpRegsLvl_(int bit, bool state) {
        // Preserve the physical bit addressed by historical H341 wide masks,
        // but touch only its byte: e.g. 0x4b addresses GP5 bit 3.
        const int offset = ((bit >> 4) & 0xf) - 1 + (bit & 0xf) / 8;
        if (offset < 0 || offset > 5) throw std::out_of_range("SCH GPIO index");
        const unsigned port = io_sch5127_regs_ + REG_GP1 + offset;
        const uint8_t mask = uint8_t{1} << (bit & 7);
        const uint8_t old = inb(port);
        const uint8_t value = state ? old | mask : old & ~mask;
        if (old != value) outb(value, port);
    }
    void setGpioSelInput_(uint32_t first, uint32_t second) {
        // Historical name: these bits are configured as OUTPUTS.
        for (unsigned bank = 0; bank < 2; ++bank) {
            const unsigned use = io_lpc_gpiobase_ + (bank ? GPIO_USE_SEL2 : GPIO_USE_SEL);
            const unsigned direction = io_lpc_gpiobase_ + (bank ? GP_IO_SEL2 : GP_IO_SEL);
            const auto mask = bank ? second : first;
            IoGrant use_grant(io_, use, 4), dir_grant(io_, direction, 4);
            doBits_(mask, use, true);
            doBits_(mask, direction, false);
        }
    }
    PortIo& io_;
    std::bitset<65536> permissions_;
    unsigned io_lpc_gpiobase_ = 0;
    unsigned io_sch5127_regs_ = 0;
    int ioperm(unsigned port, unsigned count, int enable) {
        if (port > 65535 || count > 65536 - port) throw std::out_of_range("I/O permission");
        const int result = io_.permission(port, count, enable);
        if (!result) for (unsigned i = port; i < port + count; ++i) permissions_[i] = enable;
        return result;
    }
    uint8_t inb(unsigned port) { return io_.read8(port); }
    uint32_t inl(unsigned port) { return io_.read32(port); }
    void outb(uint8_t value, unsigned port) { io_.write8(value, port); }
    void outl(uint32_t value, unsigned port) { io_.write32(value, port); }
};
