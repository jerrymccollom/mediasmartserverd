// Explicit model selection. Altered version, 2026.
#include "hardware.h"
#include "runtime.h"
#include "led_hpex485.h"
#include "led_acerh340.h"
#include "led_acerh341.h"
#include "led_acer_altos_m2.h"
#include <libudev.h>
std::string detectModel(const std::string& vendor, const std::string& product, const std::string& override_model) {
    if (!override_model.empty()) {
        if (override_model == "hp-ex48x" || override_model == "acer-h340" ||
            override_model == "acer-h341" || override_model == "acer-altos-m2") return override_model;
        throw std::invalid_argument("Unknown --model value");
    }
    if (vendor == "Acer") {
        if (product == "Aspire easyStore H340") return "acer-h340";
        if (product == "Aspire easyStore H341" || product == "Aspire easyStore H342") return "acer-h341";
        if (product == "Altos easyStore M2") return "acer-altos-m2";
    }
    if (vendor == "LENOVO" && product == "IdeaCentre D400 10023") return "acer-h340";
    if ((vendor == "HP" || vendor == "Hewlett-Packard") &&
        (product == "MediaSmart Server" || product == "EX485" || product == "EX487" ||
         product == "HP MediaSmart Server")) return "hp-ex48x";
    throw std::runtime_error("Unsupported DMI identity: " + vendor + " / " + product +
        "; use --model only after verifying the board wiring");
}
std::string dmiAttribute(const char* attribute) {
    std::unique_ptr<udev, decltype(&udev_unref)> context(udev_new(), udev_unref);
    if (!context) throw ErrnoException("udev_new");
    std::unique_ptr<udev_device, decltype(&udev_device_unref)> device(
        udev_device_new_from_subsystem_sysname(context.get(), "dmi", "id"), udev_device_unref);
    if (!device) return {};
    const char* value = udev_device_get_sysattr_value(device.get(), attribute);
    return value ? trim(value) : "";
}
LedControlPtr createHardware(const std::string& model, PortIo& io) {
    LedControlPtr control;
    if (model == "hp-ex48x") control = std::make_shared<LedHpEx48X>(io);
    else if (model == "acer-h340") control = std::make_shared<LedAcerH340>(io);
    else if (model == "acer-h341") control = std::make_shared<LedAcerH341>(io);
    else if (model == "acer-altos-m2") control = std::make_shared<LedAcerAltosM2>(io);
    else throw std::invalid_argument("Unsupported hardware model");
    if (!control->Init()) throw std::runtime_error("LPC/SCH5127 identity or register base validation failed");
    return control;
}
