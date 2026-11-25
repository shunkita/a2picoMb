/*
 * Copyright (c) 2026 a2picoMb
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * TinyUSB device configuration for a2picoMb USB Audio Device backend.
 * Ported and adapted from a2picoMb-USB for 48 kHz / 16-bit stereo UAC2 output.
 */
#ifndef _TUSB_CONFIG_H_
#define _TUSB_CONFIG_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CFG_TUSB_MCU
#error CFG_TUSB_MCU must be defined by the Pico SDK
#endif

#ifndef CFG_TUSB_OS
#define CFG_TUSB_OS OPT_OS_NONE
#endif

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG 0
#endif

#define CFG_TUD_ENABLED 1
#define CFG_TUD_ENDPOINT0_SIZE 64

#define CFG_TUD_AUDIO 1
#define CFG_TUD_CDC 1
#define CFG_TUD_MSC 0
#define CFG_TUD_HID 0
#define CFG_TUD_MIDI 0
#define CFG_TUD_VENDOR 0

/*
 * TinyUSB UAC2 device driver configured as a stereo 16-bit 48 kHz input
 * endpoint (IN to USB host). Packet lengths are paced adaptively as
 * 47, 48, or 49 stereo frames per 1 ms USB frame for clock drift compensation.
 */
#define CFG_TUD_AUDIO_FUNC_1_DESC_LEN 136
#define CFG_TUD_AUDIO_FUNC_1_N_AS_INT 1
#define CFG_TUD_AUDIO_FUNC_1_CTRL_BUF_SZ 64

#define CFG_TUD_AUDIO_ENABLE_EP_IN 1
#define CFG_TUD_AUDIO_ENABLE_EP_OUT 0
#define CFG_TUD_AUDIO_ENABLE_ENCODING 0
#define CFG_TUD_AUDIO_EP_IN_FLOW_CONTROL 0

#define CFG_TUD_AUDIO_FUNC_1_N_CHANNELS_TX 2
#define CFG_TUD_AUDIO_FUNC_1_N_BYTES_PER_SAMPLE_TX 2
/* Max packet size for 49 stereo frames (49 * 4 = 196 bytes) */
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SZ_MAX 196
#define CFG_TUD_AUDIO_FUNC_1_EP_IN_SW_BUF_SZ 2048

/* CDC diagnostic serial buffering */
#define CFG_TUD_CDC_TX_BUFSIZE 256
#define CFG_TUD_CDC_RX_BUFSIZE 64

#ifdef __cplusplus
}
#endif

#endif /* _TUSB_CONFIG_H_ */
