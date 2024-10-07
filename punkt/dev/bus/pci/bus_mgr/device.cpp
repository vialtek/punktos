/*
 * Copyright (c) 2021 Travis Geiseblrecht
 *
 * Use of this source code is governed by a MIT-style
 * license that can be found in the LICENSE file or at
 * https://opensource.org/licenses/MIT
 */

#include "device.h"

#include <sys/types.h>
#include <lk/cpp.h>
#include <lk/debug.h>
#include <lk/err.h>
#include <lk/list.h>
#include <lk/trace.h>
#include <lk/pow2.h>
#include <dev/bus/pci.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <platform/interrupts.h>

#define LOCAL_TRACE 0

#include "bus_mgr.h"
#include "bridge.h"

namespace pci {

device::device(pci_location_t loc, bus *bus) : loc_(loc), bus_(bus) {}

device::~device() {
    LTRACE;

    capability *cap;
    while ((cap = list_remove_head_type(&capability_list_, capability, node))) {
        delete cap;
    }
}

// probe the device, return a new device
status_t device::probe(pci_location_t loc, bus *parent_bus, device **out_device) {
    status_t err;

    *out_device = nullptr;

    // read vendor id and make sure this
    uint16_t vendor_id;
    err = pci_read_config_half(loc, PCI_CONFIG_VENDOR_ID, &vendor_id);
    if (err != NO_ERROR) {
        return ERR_NOT_FOUND;
    }
    if (vendor_id == 0xffff) {
        return ERR_NOT_FOUND;
    }

    // read base and sub class
    uint8_t base_class;
    err = pci_read_config_byte(loc, PCI_CONFIG_CLASS_CODE_BASE, &base_class);
    if (err != NO_ERROR) {
        return ERR_NOT_FOUND;
    }
    uint8_t sub_class;
    err = pci_read_config_byte(loc, PCI_CONFIG_CLASS_CODE_SUB, &sub_class);
    if (err != NO_ERROR) {
        return ERR_NOT_FOUND;
    }

    // read header type (0 or 1)
    uint8_t header_type;
    err = pci_read_config_byte(loc, PCI_CONFIG_HEADER_TYPE, &header_type);
    if (err != NO_ERROR) {
        return ERR_NOT_FOUND;
    }

    header_type &= PCI_HEADER_TYPE_MASK;

    if (header_type != 0) {
        LTRACEF("type %d header on device we don't understand, skipping\n", header_type);
        return ERR_NOT_FOUND;
    }

    // if it's a bridge, we should not have been called
    if (base_class == 0x6) { // XXX replace with #define
        // bridge
        if (sub_class == 0x4) { // PCI-PCI bridge, normal decode
            LTRACEF("found bridge, error\n");
            return ERR_NOT_SUPPORTED;
        }
    }

    LTRACEF_LEVEL(2, "type %#hhx\n", header_type);

    // create a new device and pass it up
    device *d = new device(loc, parent_bus);

    // try to read in the basic config space for this device
    err = d->load_config();
    if (err < 0) {
        delete d;
        return err;
    }

    // save a copy of the BARs
    d->load_bars();

    // probe the device's capabilities
    d->probe_capabilities();

    // return the newly constructed device
    *out_device = d;

    return NO_ERROR;
}

void device::dump(size_t indent) {
    for (size_t i = 0; i < indent; i++) {
        printf(" ");
    }
    char str[14];
    printf("dev %s vid:pid %04hx:%04hx base:sub:intr %hhu:%hhu:%hhu %s%s\n",
            pci_loc_string(loc_, str), config_.vendor_id, config_.device_id,
            base_class(), sub_class(), interface(),
            has_msi() ? "msi " : "",
            has_msix() ? "msix " : "");
    for (size_t b = 0; b < countof(bars_); b++) {
        if (bars_[b].valid) {
            for (size_t i = 0; i < indent + 1; i++) {
                printf(" ");
            }
            pci_dump_bar(bars_ + b, b);
        }
    }
}

status_t device::enable() {
    char str[14];
    LTRACEF("%s\n", pci_loc_string(loc(), str));

    uint16_t command;
    status_t err = pci_read_config_half(loc_, PCI_CONFIG_COMMAND, &command);
    if (err != NO_ERROR) {
        return err;
    }
    command |= PCI_COMMAND_IO_EN | PCI_COMMAND_MEM_EN | PCI_COMMAND_BUS_MASTER_EN;
    err = pci_write_config_half(loc_, PCI_CONFIG_COMMAND, command);
    if (err != NO_ERROR) {
        return err;
    }

    return NO_ERROR;
}

// walk the device's capability list, reading them in and creating sub objects per
status_t device::probe_capabilities() {
    char str[14];
    LTRACEF("%s\n", pci_loc_string(loc(), str));

    // does this device have any capabilities?
    if ((config_.status & PCI_STATUS_NEW_CAPS) == 0) {
        // no capabilities, just move on
        return NO_ERROR;
    }

    status_t err;
    size_t cap_ptr = config_.type0.capabilities_ptr; // type 0 and 1 are at same offset
    for (;;) {
        if (cap_ptr == 0) {
            break;
        }

        // read the capability id
        uint8_t cap_id;
        err = pci_read_config_byte(loc(), cap_ptr, &cap_id);
        if (err != NO_ERROR) {
            return err;
        }

        LTRACEF("cap id %#x at offset %#zx\n", cap_id, cap_ptr);

        // we only handle a few kinds of capabilities at the moment
        capability *cap = new capability;
        cap->id = cap_id;
        cap->config_offset = cap_ptr;

        // add the cap to our list
        if (cap) {
            list_add_tail(&capability_list_, &cap->node);
        }

        switch (cap_id) {
            case 0x5: { // MSI
                LTRACEF("MSI\n");
                if (init_msi_capability(cap) == NO_ERROR) {
                    msi_cap_ = cap;
                }
                break;
            }
            case 0x11: { // MSI-X
                LTRACEF("MSI-X\n");
                if (init_msix_capability(cap) == NO_ERROR) {
                    msix_cap_ = cap;
                }
                break;
            }
        }

        // read the next pointer
        uint8_t next_cap_ptr;
        err = pci_read_config_byte(loc(), cap_ptr + 1, &next_cap_ptr);
        if (err != NO_ERROR) {
            return err;
        }

        cap_ptr = next_cap_ptr;
    }

    return NO_ERROR;
}

status_t device::init_msi_capability(capability *cap) {
    LTRACE_ENTRY;

    DEBUG_ASSERT(cap->id == 0x5);

    // plain MSI
    uint32_t cap_buf[6];
    pci_read_config_word(loc(), cap->config_offset, &cap_buf[0]);
    pci_read_config_word(loc(), cap->config_offset + 4, &cap_buf[1]);
    pci_read_config_word(loc(), cap->config_offset + 8, &cap_buf[2]);
    pci_read_config_word(loc(), cap->config_offset + 12, &cap_buf[3]);
    pci_read_config_word(loc(), cap->config_offset + 16, &cap_buf[4]);
    pci_read_config_word(loc(), cap->config_offset + 20, &cap_buf[5]);
    //hexdump(cap_buf, sizeof(cap_buf));

    return NO_ERROR;
}

status_t device::init_msix_capability(capability *cap) {
    LTRACE_ENTRY;

    DEBUG_ASSERT(cap->id == 0x11);

    // MSI-X
    uint32_t cap_buf[3];
    pci_read_config_word(loc(), cap->config_offset, &cap_buf[0]);
    pci_read_config_word(loc(), cap->config_offset + 4, &cap_buf[1]);
    pci_read_config_word(loc(), cap->config_offset + 8, &cap_buf[2]);
    //hexdump(cap_buf, sizeof(cap_buf));

    return NO_ERROR;
}

status_t device::allocate_irq(uint *irq) {
    LTRACE_ENTRY;

    uint8_t interrupt_line;
    status_t err = pci_read_config_byte(loc(), PCI_CONFIG_INTERRUPT_LINE, &interrupt_line);
    if (err != NO_ERROR) return err;

    if (interrupt_line == 0) {
        return ERR_NO_RESOURCES;
    }

    // map the irq number in config space to platform vector space
    err = platform_pci_int_to_vector(interrupt_line, irq);
    return err;
}

status_t device::allocate_msi(size_t num_requested, uint *msi_base) {
    LTRACE_ENTRY;

    DEBUG_ASSERT(num_requested == 1);

    if (!has_msi()) {
        return ERR_NOT_SUPPORTED;
    }

    DEBUG_ASSERT(msi_cap_ && msi_cap_->is_msi());

    // ask the platform for interrupts
    uint vector_base;
    status_t err = platform_allocate_interrupts(num_requested, 0, true, &vector_base);
    if (err != NO_ERROR) {
        return err;
    }

    // compute the MSI message to construct
    uint64_t msi_address = 0;
    uint16_t msi_data = 0;
    err = platform_compute_msi_values(vector_base, 0, true, &msi_address, &msi_data);
    if (err != NO_ERROR) {
        // TODO: return the allocated msi
        return err;
    }

    // program it into the capability
    const uint16_t cap_offset = msi_cap_->config_offset;

    uint16_t control;
    pci_read_config_half(loc(), cap_offset + 2, &control);
    pci_write_config_half(loc(), cap_offset + 2, control & ~(0x1)); // disable MSI
    pci_write_config_word(loc(), cap_offset + 4, msi_address & 0xffff'ffff); // lower 32bits
    if (control & (1<<7)) {
        // 64bit
        pci_write_config_word(loc(), cap_offset + 8, msi_address >> 32); // upper 32bits
        pci_write_config_half(loc(), cap_offset + 0xc, msi_data);
     } else {
        pci_write_config_half(loc(), cap_offset + 8, msi_data);
    }

    // set up the control register and enable it
    control = 1; // NME/NMI = 1, no per vector masking, keep 64bit flag, enable
    pci_write_config_half(loc(), cap_offset + 2, control);

    // write it back to the pci config in the interrupt line offset
    pci_write_config_byte(loc(), PCI_CONFIG_INTERRUPT_LINE, vector_base);

    // pass back the allocated irq to the caller
    *msi_base = vector_base;

    return NO_ERROR;
}

status_t device::load_bars() {
    size_t num_bars;

    if (header_type() == 0) {
        num_bars = 6;
    } else if (header_type() == 1) {
        // type 1 only has 2 bars, but are in the same location as type0
        // so can use the same code below
        num_bars = 2;
    } else {
        // type 2 header?
        return ERR_NOT_SUPPORTED;
    }

    // Disable IO and MEM decoding around BAR detection, as we fiddle with
    // BAR addresses themselves for length detection.
    // This behavior is recommended by the PCI Local Bus Specification.

    uint16_t command;
    pci_read_config_half(loc(), PCI_CONFIG_COMMAND, &command);
    pci_write_config_half(loc(), PCI_CONFIG_COMMAND, command & ~(PCI_COMMAND_IO_EN | PCI_COMMAND_MEM_EN));

    for (size_t i=0; i < num_bars; i++) {
        bars_[i] = {};
        uint64_t bar_addr = config_.type0.base_addresses[i];
        if (bar_addr & 0x1) {
            // io address
            bars_[i].io = true;
            bars_[i].prefetchable = false;
            bars_[i].size_64 = false;
            bars_[i].addr = bar_addr & ~0x3;

            // probe size by writing all 1s and seeing what bits are masked
            uint32_t size = 0;
            pci_write_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4, 0xffff);
            pci_read_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4, &size);
            pci_write_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4, bars_[i].addr);

            // mask out bottom bits, invert and add 1 to compute size
            bars_[i].size = ((size & ~0b11) ^ 0xffff) + 1;

            bars_[i].valid = (bars_[i].size != 0);
        } else if ((bar_addr & 0b110) == 0b000) {
            // 32bit memory address
            bars_[i].io = false;
            bars_[i].prefetchable = bar_addr & (1<<3);
            bars_[i].size_64 = false;
            bars_[i].addr = bar_addr & ~0xf;

            // probe size by writing all 1s and seeing what bits are masked
            uint32_t size = 0;
            pci_write_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4, 0xffffffff);
            pci_read_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4, &size);
            pci_write_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4, bars_[i].addr);

            // mask out bottom bits, invert and add 1 to compute size
            bars_[i].size = (~(size & ~0b1111)) + 1;

            bars_[i].valid = (bars_[i].size != 0);
        } else if ((bar_addr & 0b110) == 0b100) {
            // 64bit memory address
            if (i >= num_bars - 1) {
                // root of 64bit memory range will use up two slots, so cant
                // start at the last bar
                continue;
            }
            bars_[i].io = false;
            bars_[i].prefetchable = bar_addr & (1<<3);
            bars_[i].size_64 = true;
            bars_[i].addr = bar_addr & ~0xf;
            bars_[i].addr |= (uint64_t)config_.type0.base_addresses[i + 1] << 32;

            // probe size by writing all 1s and seeing what bits are masked
            uint64_t size;
            uint32_t size32 = 0;
            pci_write_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4, 0xffffffff);
            pci_read_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4, &size32);
            size = size32;
            pci_write_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4 + 4, 0xffffffff);
            pci_read_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4 + 4, &size32);
            size |= (uint64_t)size32 << 32;
            pci_write_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4, bars_[i].addr);
            pci_write_config_word(loc_, PCI_CONFIG_BASE_ADDRESSES + i * 4 + 4, bars_[i].addr >> 32);

            // mask out bottom bits, invert and add 1 to compute size
            bars_[i].size = (~(size & ~(uint64_t)0b1111)) + 1;

            bars_[i].valid = (bars_[i].size != 0);

            // mark the next entry as invalid
            i++;
            bars_[i] = {}; // clears the valid bit
        }
    }

    // Restore any IO and MEM decoding that was enabled before
    pci_write_config_half(loc(), PCI_CONFIG_COMMAND, command);

    return NO_ERROR;
}

status_t device::read_bars(pci_bar_t bar[6]) {
    // copy the cached bar information
    memcpy(bar, bars_, sizeof(bars_));
    return NO_ERROR;
}

status_t device::load_config() {
    status_t err = pci_read_config(loc_, &config_);
    return err;
}

status_t device::compute_bar_sizes(bar_sizes *sizes) {
    char str[14];
    LTRACEF("device at %s\n", pci_loc_string(loc(), str));

    // iterate through the bars on this device and accumulate the size
    // of all the bars of various types. also accumulate the maximum alignment
    for (auto i = 0; i < 6; i++) {
        const auto &bar = bars_[i];
        if (!bar.valid) {
            continue;
        }

        if (bar.io) {
            // io case
            sizes->io_size += ROUNDUP(bar.size, 16);
            if (sizes->io_align < 4) {
                sizes->io_align = 4;
            }
        } else if (bar.size_64 && bar.prefetchable) {
            // 64bit mmio
            auto size = ROUNDUP(bar.size, PAGE_SIZE);
            auto align = __builtin_ctz(size);
            sizes->prefetchable64_size += size;
            if (sizes->prefetchable64_align < align) {
                sizes->prefetchable64_align = align;
            }
        } else if (bar.size_64) {
            // 64bit mmio
            auto size = ROUNDUP(bar.size, PAGE_SIZE);
            auto align = __builtin_ctz(size);
            sizes->mmio64_size += size;
            if (sizes->mmio64_align < align) {
                sizes->mmio64_align = align;
            }
        } else if (bar.prefetchable) {
            // 64bit prefetchable mmio
            auto size = ROUNDUP(bar.size, PAGE_SIZE);
            auto align = __builtin_ctz(size);
            sizes->prefetchable_size += size;
            if (sizes->prefetchable_align < align) {
                sizes->prefetchable_align = align;
            }
        } else {
            // 32bit mmio
            auto size = ROUNDUP(bar.size, PAGE_SIZE);
            auto align = __builtin_ctz(size);
            sizes->mmio_size += size;
            if (sizes->mmio_align < align) {
                sizes->mmio_align = align;
            }
        }
    }

    return NO_ERROR;
}

status_t device::get_bar_alloc_requests(list_node *bar_alloc_requests) {
    char str[14];
    LTRACEF("device at %s\n", pci_loc_string(loc(), str));

    DEBUG_ASSERT(bar_alloc_requests);

    // iterate through the bars on this device and accumulate the size
    // of all the bars of various types. also accumulate the maximum alignment
    for (auto i = 0; i < 6; i++) {
        const auto &bar = bars_[i];
        if (!bar.valid) {
            continue;
        }

        auto request = new bar_alloc_request;
        *request = {};
        request->bridge = false;
        request->dev = this;
        request->bar_num = i;

        if (bar.io) {
            // io case
            request->size = ROUNDUP(bar.size, 16);
            request->align = 4;
            request->type = PCI_RESOURCE_IO_RANGE;
        } else if (bar.size_64) {
            // 64bit mmio
            auto size = ROUNDUP(bar.size, PAGE_SIZE);
            auto align = __builtin_ctz(size);
            request->size = size;
            request->align = align;
            request->type = PCI_RESOURCE_MMIO64_RANGE;
            request->prefetchable = bar.prefetchable;
        } else {
            // 32bit mmio
            auto size = ROUNDUP(bar.size, PAGE_SIZE);
            auto align = __builtin_ctz(size);
            request->size = size;
            request->align = align;
            request->type = PCI_RESOURCE_MMIO_RANGE;
            request->prefetchable = bar.prefetchable;
        }
        // add it to the list passed in
        list_add_tail(bar_alloc_requests, &request->node);
    }

    return NO_ERROR;
}

status_t device::assign_resource(bar_alloc_request *request, uint64_t address) {
    char str[14];
    LTRACEF("device at %s resource addr %#llx request:\n", pci_loc_string(loc(), str), address);
    if (LOCAL_TRACE) {
        request->dump();
    }

    DEBUG_ASSERT(IS_ALIGNED(address, (1UL << request->align)));

    // Note: When assigning the resource, we don't bother setting the bottom bits
    // as those are hardwired per the spec.

    uint32_t temp;
    switch (request->type) {
        case PCI_RESOURCE_IO_RANGE:
            temp = (address & 0xfffc);
            pci_write_config_word(loc(), PCI_CONFIG_BASE_ADDRESSES + request->bar_num * 4, temp);
            break;
        case PCI_RESOURCE_MMIO_RANGE:
            temp = (address & 0xfffffff0);
            pci_write_config_word(loc(), PCI_CONFIG_BASE_ADDRESSES + request->bar_num * 4, temp);
            break;
        case PCI_RESOURCE_MMIO64_RANGE:
            temp = (address & 0xfffffff0);
            pci_write_config_word(loc(), PCI_CONFIG_BASE_ADDRESSES + request->bar_num * 4, temp);
            temp = address >> 32;
            pci_write_config_word(loc(), PCI_CONFIG_BASE_ADDRESSES + request->bar_num * 4 + 4, temp);
            break;
        default:
            panic("invalid request type %d\n", request->type);
    }

    load_config();
    load_bars();

    return NO_ERROR;
}

void device::bar_alloc_request::dump() {
    char str[14];
    if (bridge) {
        printf("BAR alloc request %p: bridge %s type %u (%s) pref %d size %#llx align %u\n",
                this, pci_loc_string(dev->loc(), str), type, pci_resource_type_to_str(type), prefetchable, size, align);
    } else {
        printf("BAR alloc request %p: device %s type %u (%s) pref %d size %#llx align %u bar %u\n",
                this, pci_loc_string(dev->loc(), str), type, pci_resource_type_to_str(type), prefetchable, size, align, bar_num);
    }
}

} // namespace pci
