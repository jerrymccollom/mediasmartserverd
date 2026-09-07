// Injectable hardware access. Altered version, 2026.
#pragma once
#include "errno_exception.h"
#include <bitset>
#include <cstdint>
#include <sys/io.h>
#include <sys/syscall.h>
#include <unistd.h>

class PortIo {
public:
    virtual ~PortIo() = default;
    virtual int permission(unsigned port, unsigned count, bool enable) = 0;
    virtual uint8_t read8(unsigned port) = 0;
    virtual uint32_t read32(unsigned port) = 0;
    virtual void write8(uint8_t value, unsigned port) = 0;
    virtual void write32(uint32_t value, unsigned port) = 0;
};
class NativePortIo : public PortIo {
    const long owner_ = syscall(SYS_gettid);
    void check(unsigned port, unsigned count) const {
        if (syscall(SYS_gettid) != owner_ || port > 65535 || count > 65536 - port)
            throw std::runtime_error("Invalid port access or hardware access from non-owner thread");
    }
public:
    int permission(unsigned port, unsigned count, bool enable) override {
        check(port, count); return ::ioperm(port, count, enable);
    }
    uint8_t read8(unsigned port) override { check(port, 1); return ::inb(static_cast<uint16_t>(port)); }
    uint32_t read32(unsigned port) override { check(port, 4); return ::inl(static_cast<uint16_t>(port)); }
    void write8(uint8_t value, unsigned port) override { check(port, 1); ::outb(value, static_cast<uint16_t>(port)); }
    void write32(uint32_t value, unsigned port) override { check(port, 4); ::outl(value, static_cast<uint16_t>(port)); }
    static PortIo& instance() { static NativePortIo io; return io; }
};
class IoGrant {
    PortIo& io_; unsigned port_, count_;
public:
    IoGrant(PortIo& io, unsigned port, unsigned count) : io_(io), port_(port), count_(count) {
        if (io_.permission(port_, count_, true)) throw ErrnoException("ioperm");
    }
    ~IoGrant() { try { io_.permission(port_, count_, false); } catch (...) {} }
    IoGrant(const IoGrant&) = delete;
    IoGrant& operator=(const IoGrant&) = delete;
};
