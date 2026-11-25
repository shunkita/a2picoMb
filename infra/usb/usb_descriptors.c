/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

#include "tusb.h"

#include "pico/unique_id.h"
#include <stdio.h>
#include <string.h>

#define USB_AUDIO_RATE_HZ 48000u
#define USB_AUDIO_CHANNELS 2u
#define USB_AUDIO_BYTES_PER_SAMPLE 2u
#define USB_AUDIO_EP_SIZE \
    TUD_AUDIO_EP_SIZE(USB_AUDIO_RATE_HZ, USB_AUDIO_BYTES_PER_SAMPLE, USB_AUDIO_CHANNELS)

enum {
    ITF_NUM_AUDIO_CONTROL = 0,
    ITF_NUM_AUDIO_STREAMING = 1,
    ITF_NUM_CDC_CONTROL = 2,
    ITF_NUM_CDC_DATA = 3,
    ITF_NUM_TOTAL = 4,
};

enum {
    STRID_LANGID = 0,
    STRID_MANUFACTURER = 1,
    STRID_PRODUCT = 2,
    STRID_SERIAL = 3,
    STRID_CDC_INTERFACE = 4,
    STRID_AUDIO_CLOCK = 5,
    STRID_AUDIO_INPUT = 6,
    STRID_AUDIO_OUTPUT = 7,
};

#define USB_AUDIO_CONTROL_ENTITY_LEN \
    (TUD_AUDIO_DESC_CLK_SRC_LEN + TUD_AUDIO_DESC_INPUT_TERM_LEN + \
     TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL_LEN + TUD_AUDIO_DESC_OUTPUT_TERM_LEN)

#define USB_AUDIO_FUNCTION_DESC_LEN \
    (TUD_AUDIO_DESC_IAD_LEN + TUD_AUDIO_DESC_STD_AC_LEN + TUD_AUDIO_DESC_CS_AC_LEN + \
     USB_AUDIO_CONTROL_ENTITY_LEN + 2 * TUD_AUDIO_DESC_STD_AS_INT_LEN + \
     TUD_AUDIO_DESC_CS_AS_INT_LEN + TUD_AUDIO_DESC_TYPE_I_FORMAT_LEN + \
     TUD_AUDIO_DESC_STD_AS_ISO_EP_LEN + TUD_AUDIO_DESC_CS_AS_ISO_EP_LEN)

#define USB_CONFIGURATION_DESC_LEN \
    (TUD_CONFIG_DESC_LEN + USB_AUDIO_FUNCTION_DESC_LEN + TUD_CDC_DESC_LEN)

_Static_assert(USB_AUDIO_EP_SIZE == 196, "TinyUSB endpoint sizing must match 49 frames (196 bytes)");
_Static_assert(USB_AUDIO_FUNCTION_DESC_LEN == 136,
               "Update CFG_TUD_AUDIO_FUNC_1_DESC_LEN when descriptors change");

static tusb_desc_device_t const device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = TUSB_CLASS_MISC,
    .bDeviceSubClass = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol = MISC_PROTOCOL_IAD,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    /* Development VID/PID; 0x4011 identifies the Audio + CDC interface set. */
    .idVendor = 0xCAFE,
    .idProduct = 0x4011,
    .bcdDevice = 0x0100,
    .iManufacturer = STRID_MANUFACTURER,
    .iProduct = STRID_PRODUCT,
    .iSerialNumber = STRID_SERIAL,
    .bNumConfigurations = 0x01,
};

uint8_t const *tud_descriptor_device_cb(void) {
    return (uint8_t const *)&device_descriptor;
}

static uint8_t const configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, USB_CONFIGURATION_DESC_LEN, 0x00, 100),

    TUD_AUDIO_DESC_IAD(ITF_NUM_AUDIO_CONTROL, 2, STRID_PRODUCT),
    TUD_AUDIO_DESC_STD_AC(ITF_NUM_AUDIO_CONTROL, 0, STRID_PRODUCT),
    TUD_AUDIO_DESC_CS_AC(0x0200, AUDIO_FUNC_MICROPHONE,
                         USB_AUDIO_CONTROL_ENTITY_LEN, AUDIO_CS_AS_INTERFACE_CTRL_LATENCY_POS),
    TUD_AUDIO_DESC_CLK_SRC(4, AUDIO_CLOCK_SOURCE_ATT_INT_FIX_CLK,
                           (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_FRQ_POS) |
                               (AUDIO_CTRL_R << AUDIO_CLOCK_SOURCE_CTRL_CLK_VAL_POS),
                           0, STRID_AUDIO_CLOCK),
    TUD_AUDIO_DESC_INPUT_TERM(1, AUDIO_TERM_TYPE_IN_GENERIC_MIC, 0, 4, 2,
                              AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT,
                              0, AUDIO_CTRL_NONE, STRID_AUDIO_INPUT),
    TUD_AUDIO_DESC_FEATURE_UNIT_TWO_CHANNEL(2, 1, AUDIO_CTRL_NONE, AUDIO_CTRL_NONE,
                                             AUDIO_CTRL_NONE, 0),
    TUD_AUDIO_DESC_OUTPUT_TERM(3, AUDIO_TERM_TYPE_USB_STREAMING, 0, 2, 4, 0, STRID_AUDIO_OUTPUT),

    TUD_AUDIO_DESC_STD_AS_INT(ITF_NUM_AUDIO_STREAMING, 0, 0, 0),
    TUD_AUDIO_DESC_STD_AS_INT(ITF_NUM_AUDIO_STREAMING, 1, 1, 0),
    TUD_AUDIO_DESC_CS_AS_INT(3, AUDIO_CTRL_NONE, AUDIO_FORMAT_TYPE_I,
                             AUDIO_DATA_FORMAT_TYPE_I_PCM, USB_AUDIO_CHANNELS,
                             AUDIO_CHANNEL_CONFIG_FRONT_LEFT | AUDIO_CHANNEL_CONFIG_FRONT_RIGHT,
                             0),
    TUD_AUDIO_DESC_TYPE_I_FORMAT(USB_AUDIO_BYTES_PER_SAMPLE, 16),
    TUD_AUDIO_DESC_STD_AS_ISO_EP(
        0x81,
        (uint8_t)(TUSB_XFER_ISOCHRONOUS | TUSB_ISO_EP_ATT_ASYNCHRONOUS |
                  TUSB_ISO_EP_ATT_DATA),
        USB_AUDIO_EP_SIZE, 1),
    TUD_AUDIO_DESC_CS_AS_ISO_EP(AUDIO_CS_AS_ISO_DATA_EP_ATT_NON_MAX_PACKETS_OK,
                                AUDIO_CTRL_NONE,
                                AUDIO_CS_AS_ISO_DATA_EP_LOCK_DELAY_UNIT_UNDEFINED, 0),

    /* CDC diagnostic serial function; endpoint numbers are distinct from audio. */
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC_CONTROL, STRID_CDC_INTERFACE, 0x83, 8, 0x04, 0x85, 64),
};

uint8_t const *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return configuration_descriptor;
}

uint16_t const *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    static uint16_t desc_str[32];
    char serial_str[2 * PICO_UNIQUE_BOARD_ID_SIZE_BYTES + 1];
    char product_str[32];
    char const *str = NULL;

    pico_get_unique_board_id_string(serial_str, sizeof(serial_str));
    size_t serial_len = strlen(serial_str);
    char const *id_suffix = (serial_len >= 4) ? &serial_str[serial_len - 4] : serial_str;

    switch (index) {
    case STRID_LANGID:
        desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | 4);
        desc_str[1] = 0x0409;
        return desc_str;
    case STRID_MANUFACTURER:
        str = "a2picoMb";
        break;
    case STRID_PRODUCT:
        snprintf(product_str, sizeof(product_str), "A2PicoMb USB Audio (%s)", id_suffix);
        str = product_str;
        break;
    case STRID_SERIAL:
        str = serial_str;
        break;
    case STRID_CDC_INTERFACE:
        str = "A2PicoMb Diagnostics";
        break;
    case STRID_AUDIO_CLOCK:
        snprintf(product_str, sizeof(product_str), "A2Pico Clock (%s)", id_suffix);
        str = product_str;
        break;
    case STRID_AUDIO_INPUT:
        snprintf(product_str, sizeof(product_str), "A2Pico Input (%s)", id_suffix);
        str = product_str;
        break;
    case STRID_AUDIO_OUTPUT:
        snprintf(product_str, sizeof(product_str), "A2Pico Stream (%s)", id_suffix);
        str = product_str;
        break;
    default:
        return NULL;
    }

    size_t length = strlen(str);
    if (length > 31) {
        length = 31;
    }
    for (size_t i = 0; i < length; i++) {
        desc_str[i + 1] = (uint8_t)str[i];
    }
    desc_str[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * length + 2));
    return desc_str;
}
