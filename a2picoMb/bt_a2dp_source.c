/*

MIT License

Copyright (c) 2026 Shunichi Kitahara

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "pico/stdlib.h"
#include "pico/cyw43_arch.h"
#include "btstack.h"
#include "classic/a2dp_source.h"
#include "classic/avdtp_util.h"
#include "classic/btstack_sbc.h"
#include "bt_a2dp_source.h"

#define AUDIO_TIMEOUT_MS 20
#define SBC_STORAGE_SIZE 1030
#define NUM_CHANNELS 2

typedef enum {
    PACKET_STATE_IDLE = 0,
    PACKET_STATE_READY_TO_SEND,
} packet_state_t;

typedef struct {
    uint16_t a2dp_cid;
    uint8_t local_seid;
    uint8_t remote_seid;
    int stream_opened;
    int streaming;
    uint32_t rtp_timestamp;
    btstack_timer_source_t audio_timer;
    uint32_t stream_start_ms;
    uint32_t packets_scheduled;
    uint32_t stat_packets_generated;
    uint32_t stat_tx_wait_count;
    packet_state_t packet_state;
    uint8_t sbc_storage[SBC_STORAGE_SIZE];
    uint16_t sbc_storage_count;
    uint16_t max_media_payload_size;
} a2dp_source_context_t;

typedef struct {
    int reconfigure;
    int num_channels;
    int sampling_frequency;
    int block_length;
    int subbands;
    int min_bitpool_value;
    int max_bitpool_value;
    btstack_sbc_channel_mode_t      channel_mode;
    btstack_sbc_allocation_method_t allocation_method;
} media_codec_configuration_sbc_t;

static uint8_t media_sbc_codec_capabilities[] = {
    (AVDTP_SBC_44100 << 4) | AVDTP_SBC_STEREO | AVDTP_SBC_JOINT_STEREO,
    0xFF,
    2, 53
};

static uint8_t media_sbc_codec_configuration[4];
static media_codec_configuration_sbc_t sbc_configuration;
static const btstack_sbc_encoder_t *sbc_encoder_instance = NULL;
static btstack_sbc_encoder_bluedroid_t sbc_encoder_state;

static uint8_t sdp_a2dp_source_service_buffer[150];
static a2dp_source_context_t media_tracker;
static bt_a2dp_pcm_callback_t pcm_data_callback = NULL;

static bd_addr_t remote_device_addr;
static bool scan_active = false;
static bool target_connected = false;

static void a2dp_source_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);
static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size);

static btstack_packet_callback_registration_t hci_event_callback_registration;

static void a2dp_start_scanning(void) {
    printf("[BT] Start scanning for Bluetooth speakers...\n");
    gap_inquiry_start(12); // 12 * 1.28s ~ 15s
    scan_active = true;
}

static uint32_t bt_packet_send_count = 0;
static uint8_t bt_last_send_status = 0;
static uint32_t bt_timer_tick_count = 0;

static void send_media_packet(void) {
    if (!sbc_encoder_instance) return;
    if (media_tracker.packet_state != PACKET_STATE_READY_TO_SEND) return;

    int num_bytes_in_frame = sbc_encoder_instance->sbc_buffer_length(&sbc_encoder_state);
    int bytes_in_storage = media_tracker.sbc_storage_count;
    if (bytes_in_storage == 0) return;
    uint8_t num_sbc_frames = bytes_in_storage / num_bytes_in_frame;

    media_tracker.sbc_storage[0] = num_sbc_frames;
    bt_last_send_status = a2dp_source_stream_send_media_payload_rtp(media_tracker.a2dp_cid, media_tracker.local_seid, 0,
                                                                    media_tracker.rtp_timestamp,
                                                                    media_tracker.sbc_storage, bytes_in_storage + 1);
    bt_packet_send_count++;

    unsigned int num_audio_samples_per_sbc_buffer = sbc_encoder_instance->num_audio_frames(&sbc_encoder_state);
    media_tracker.rtp_timestamp += num_sbc_frames * num_audio_samples_per_sbc_buffer;
    media_tracker.sbc_storage_count = 0;
    media_tracker.packet_state = PACKET_STATE_IDLE;

    // IMPORTANT: Do NOT generate next packet here; let the 44.1kHz absolute timer handle it.
}

static uint32_t last_total_handler_us = 0;
static uint32_t last_pcm_time_us = 0;
static uint32_t last_sbc_time_us = 0;

static int fill_sbc_audio_buffer(a2dp_source_context_t *context) {
    if (!sbc_encoder_instance || !pcm_data_callback) return 0;
    int total_samples_read = 0;
    unsigned int num_audio_samples = sbc_encoder_instance->num_audio_frames(&sbc_encoder_state);
    uint16_t sbc_buffer_length = sbc_encoder_instance->sbc_buffer_length(&sbc_encoder_state);

    while ((context->max_media_payload_size - context->sbc_storage_count) >= sbc_buffer_length) {
        int16_t pcm_frame[256 * NUM_CHANNELS];
        pcm_data_callback(pcm_frame, num_audio_samples);

        sbc_encoder_instance->encode_signed_16(&sbc_encoder_state, pcm_frame,
                                               &context->sbc_storage[1 + context->sbc_storage_count]);

        total_samples_read += num_audio_samples;
        context->sbc_storage_count += sbc_encoder_instance->sbc_buffer_length(&sbc_encoder_state);
    }
    return total_samples_read;
}

static void audio_timeout_handler(btstack_timer_source_t *timer) {
    bt_timer_tick_count++;
    a2dp_source_context_t *context = (a2dp_source_context_t *)btstack_run_loop_get_timer_context(timer);

    if (!context->streaming) return;

    // 1. Calculate next target time based on absolute timeline (zero cumulative drift)
    // 1 packet = 640 samples = 6400 / 441 ms (~14.51247 ms)
    context->packets_scheduled++;
    uint32_t next_target_ms = context->stream_start_ms + (uint32_t)((uint64_t)context->packets_scheduled * 6400ULL / 441ULL);
    uint32_t now_ms = btstack_run_loop_get_time_ms();

    uint32_t delay_ms = 1;
    if (next_target_ms > now_ms) {
        delay_ms = next_target_ms - now_ms;
    }

    btstack_run_loop_set_timer(&context->audio_timer, delay_ms);
    btstack_run_loop_add_timer(&context->audio_timer);

    // 2. Single buffer protection: if previous packet is still pending transmission
    if (context->packet_state != PACKET_STATE_IDLE) {
        context->stat_tx_wait_count++;
        // Hold in pending state; never overwrite or duplicate packet generation.
        return;
    }

    // 3. Generate 640 samples (5 SBC frames)
    fill_sbc_audio_buffer(context);
    context->stat_packets_generated++;
    context->packet_state = PACKET_STATE_READY_TO_SEND;

    // 4. Request can-send-now from BTstack
    a2dp_source_stream_endpoint_request_can_send_now(context->a2dp_cid, context->local_seid);
}

static void a2dp_timer_start(a2dp_source_context_t *context) {
    context->max_media_payload_size = btstack_min(a2dp_max_media_payload_size(context->a2dp_cid, context->local_seid), SBC_STORAGE_SIZE);
    context->sbc_storage_count = 0;
    context->packet_state = PACKET_STATE_IDLE;
    context->streaming = 1;
    context->stream_start_ms = btstack_run_loop_get_time_ms();
    context->packets_scheduled = 0;
    context->stat_packets_generated = 0;
    context->stat_tx_wait_count = 0;

    // Generate initial packet (N=0) and request send
    fill_sbc_audio_buffer(context);
    context->stat_packets_generated++;
    context->packet_state = PACKET_STATE_READY_TO_SEND;
    a2dp_source_stream_endpoint_request_can_send_now(context->a2dp_cid, context->local_seid);

    // Calculate next target time (N=1) and set timer
    context->packets_scheduled = 1;
    uint32_t next_target_ms = context->stream_start_ms + (uint32_t)((uint64_t)context->packets_scheduled * 6400ULL / 441ULL);
    uint32_t now_ms = btstack_run_loop_get_time_ms();
    uint32_t delay_ms = (next_target_ms > now_ms) ? (next_target_ms - now_ms) : 1;

    btstack_run_loop_remove_timer(&context->audio_timer);
    btstack_run_loop_set_timer_handler(&context->audio_timer, audio_timeout_handler);
    btstack_run_loop_set_timer_context(&context->audio_timer, context);
    btstack_run_loop_set_timer(&context->audio_timer, delay_ms);
    btstack_run_loop_add_timer(&context->audio_timer);
}

static void hci_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;

    bd_addr_t address;
    uint32_t cod;
    const uint32_t bluetooth_speaker_cod = 0x200000 | 0x040000 | 0x000400; // Rendering | Audio

    switch (hci_event_packet_get_type(packet)) {
    case BTSTACK_EVENT_STATE:
        if (btstack_event_state_get_state(packet) == HCI_STATE_WORKING) {
            printf("[BT] BTstack HCI working. Starting inquiry...\n");
            a2dp_start_scanning();
        }
        break;

    case HCI_EVENT_PIN_CODE_REQUEST:
        printf("[BT] PIN code request, responding '0000'\n");
        hci_event_pin_code_request_get_bd_addr(packet, address);
        gap_pin_code_response(address, "0000");
        break;

    case GAP_EVENT_INQUIRY_RESULT:
        gap_event_inquiry_result_get_bd_addr(packet, address);
        cod = gap_event_inquiry_result_get_class_of_device(packet);
        printf("[BT] Device found: %s with COD: %06" PRIx32 "\n", bd_addr_to_str(address), cod);

        if (!target_connected && (cod & bluetooth_speaker_cod) == bluetooth_speaker_cod) {
            memcpy(remote_device_addr, address, 6);
            printf("[BT] Audio speaker detected: %s. Connecting...\n", bd_addr_to_str(remote_device_addr));
            scan_active = false;
            target_connected = true;
            gap_inquiry_stop();
            a2dp_source_establish_stream(remote_device_addr, &media_tracker.a2dp_cid);
        }
        break;

    case GAP_EVENT_INQUIRY_COMPLETE:
        if (scan_active && !target_connected && !media_tracker.streaming && media_tracker.a2dp_cid == 0) {
            printf("[BT] Inquiry complete, no speaker connected. Scanning again...\n");
            a2dp_start_scanning();
        }
        break;

    default:
        break;
    }
}

static void a2dp_source_packet_handler(uint8_t packet_type, uint16_t channel, uint8_t *packet, uint16_t size) {
    UNUSED(channel);
    UNUSED(size);
    if (packet_type != HCI_EVENT_PACKET) return;
    if (hci_event_packet_get_type(packet) != HCI_EVENT_A2DP_META) return;

    bd_addr_t address;
    uint8_t status;
    uint16_t cid;

    switch (hci_event_a2dp_meta_get_subevent_code(packet)) {
    case A2DP_SUBEVENT_SIGNALING_CONNECTION_ESTABLISHED:
        a2dp_subevent_signaling_connection_established_get_bd_addr(packet, address);
        cid = a2dp_subevent_signaling_connection_established_get_a2dp_cid(packet);
        status = a2dp_subevent_signaling_connection_established_get_status(packet);
        if (status != ERROR_CODE_SUCCESS) {
            printf("[BT] A2DP connection failed: 0x%02x\n", status);
            media_tracker.a2dp_cid = 0;
            target_connected = false;
            a2dp_start_scanning();
            break;
        }
        media_tracker.a2dp_cid = cid;
        target_connected = true;
        scan_active = false;
        gap_inquiry_stop();
        printf("[BT] A2DP signaling connected to %s (cid 0x%02x)\n", bd_addr_to_str(address), cid);
        break;

    case A2DP_SUBEVENT_SIGNALING_MEDIA_CODEC_SBC_CONFIGURATION:
        cid = avdtp_subevent_signaling_media_codec_sbc_configuration_get_avdtp_cid(packet);
        if (cid != media_tracker.a2dp_cid) return;

        media_tracker.remote_seid = a2dp_subevent_signaling_media_codec_sbc_configuration_get_remote_seid(packet);
        sbc_configuration.sampling_frequency = a2dp_subevent_signaling_media_codec_sbc_configuration_get_sampling_frequency(packet);
        sbc_configuration.block_length = a2dp_subevent_signaling_media_codec_sbc_configuration_get_block_length(packet);
        sbc_configuration.subbands = a2dp_subevent_signaling_media_codec_sbc_configuration_get_subbands(packet);
        sbc_configuration.max_bitpool_value = a2dp_subevent_signaling_media_codec_sbc_configuration_get_max_bitpool_value(packet);

        avdtp_channel_mode_t cm = (avdtp_channel_mode_t)a2dp_subevent_signaling_media_codec_sbc_configuration_get_channel_mode(packet);
        switch (cm) {
        case AVDTP_CHANNEL_MODE_JOINT_STEREO:
            sbc_configuration.channel_mode = SBC_CHANNEL_MODE_JOINT_STEREO;
            break;
        case AVDTP_CHANNEL_MODE_STEREO:
            sbc_configuration.channel_mode = SBC_CHANNEL_MODE_STEREO;
            break;
        default:
            sbc_configuration.channel_mode = SBC_CHANNEL_MODE_STEREO;
            break;
        }
        sbc_configuration.allocation_method = (btstack_sbc_allocation_method_t)(a2dp_subevent_signaling_media_codec_sbc_configuration_get_allocation_method(packet) - 1);

        printf("[BT] SBC configured: %u Hz, block %d, subbands %d\n",
               sbc_configuration.sampling_frequency, sbc_configuration.block_length, sbc_configuration.subbands);

        sbc_encoder_instance = btstack_sbc_encoder_bluedroid_init_instance(&sbc_encoder_state);
        sbc_encoder_instance->configure(&sbc_encoder_state, SBC_MODE_STANDARD,
                                        sbc_configuration.block_length, sbc_configuration.subbands,
                                        sbc_configuration.allocation_method, sbc_configuration.sampling_frequency,
                                        sbc_configuration.max_bitpool_value,
                                        sbc_configuration.channel_mode);
        break;

    case A2DP_SUBEVENT_STREAM_ESTABLISHED:
        status = a2dp_subevent_stream_established_get_status(packet);
        if (status != ERROR_CODE_SUCCESS) {
            printf("[BT] A2DP stream failed: 0x%02x\n", status);
            break;
        }
        media_tracker.stream_opened = 1;
        printf("[BT] A2DP stream established. Starting stream...\n");
        a2dp_source_start_stream(media_tracker.a2dp_cid, media_tracker.local_seid);
        break;

    case A2DP_SUBEVENT_STREAM_STARTED:
        printf("[BT] A2DP stream started! Audio output active.\n");
        scan_active = false;
        gap_inquiry_stop();
        a2dp_timer_start(&media_tracker);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
        break;

    case A2DP_SUBEVENT_STREAMING_CAN_SEND_MEDIA_PACKET_NOW:
        send_media_packet();
        break;

    case A2DP_SUBEVENT_SIGNALING_CONNECTION_RELEASED:
    case A2DP_SUBEVENT_STREAM_RELEASED:
        printf("[BT] A2DP stream released.\n");
        media_tracker.streaming = 0;
        media_tracker.stream_opened = 0;
        media_tracker.a2dp_cid = 0;
        target_connected = false;
        btstack_run_loop_remove_timer(&media_tracker.audio_timer);
        cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
        a2dp_start_scanning();
        break;

    default:
        break;
    }
}

bool bt_a2dp_source_init(bt_a2dp_pcm_callback_t callback) {
    pcm_data_callback = callback;
    memset(&media_tracker, 0, sizeof(media_tracker));

    // Initialize CYW43 architecture
    if (cyw43_arch_init()) {
        printf("[BT] Failed to initialize cyw43_arch\n");
        return false;
    }
    // Ensure LED is OFF initially
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

    // Initialize BTstack L2CAP & SDP
    l2cap_init();
    sdp_init();

    // Initialize A2DP Source
    a2dp_source_init();
    a2dp_source_register_packet_handler(&a2dp_source_packet_handler);

    avdtp_stream_endpoint_t *local_stream_endpoint = a2dp_source_create_stream_endpoint(
        AVDTP_AUDIO, AVDTP_CODEC_SBC, media_sbc_codec_capabilities,
        sizeof(media_sbc_codec_capabilities), media_sbc_codec_configuration,
        sizeof(media_sbc_codec_configuration));

    if (!local_stream_endpoint) {
        printf("[BT] Failed to create stream endpoint\n");
        return false;
    }

    avdtp_set_preferred_sampling_frequency(local_stream_endpoint, 44100);
    media_tracker.local_seid = avdtp_local_seid(local_stream_endpoint);

    // Register A2DP Source SDP service record
    memset(sdp_a2dp_source_service_buffer, 0, sizeof(sdp_a2dp_source_service_buffer));
    a2dp_source_create_sdp_record(sdp_a2dp_source_service_buffer, sdp_create_service_record_handle(),
                                  AVDTP_SOURCE_FEATURE_MASK_PLAYER, NULL, NULL);
    sdp_register_service(sdp_a2dp_source_service_buffer);

    // Register HCI packet handler
    hci_event_callback_registration.callback = &hci_packet_handler;
    hci_add_event_handler(&hci_event_callback_registration);

    // Turn on Bluetooth
    hci_power_control(HCI_POWER_ON);

    printf("[BT] A2DP Source initialized successfully.\n");
    return true;
}

void bt_a2dp_source_poll(void) {
    cyw43_arch_poll();
}

bool bt_a2dp_source_is_streaming(void) {
    return media_tracker.streaming != 0;
}

uint32_t bt_a2dp_get_packets_sent(void) {
    return bt_packet_send_count;
}

uint8_t bt_a2dp_get_last_send_status(void) {
    return bt_last_send_status;
}

uint32_t bt_a2dp_get_timer_ticks(void) {
    return bt_timer_tick_count;
}

void bt_a2dp_get_frame_breakdown(uint32_t *total_us, uint32_t *pcm_us, uint32_t *sbc_us) {
    if (total_us) *total_us = 0;
    if (pcm_us) *pcm_us = 0;
    if (sbc_us) *sbc_us = 0;
}

void bt_a2dp_get_pacing_stats(uint32_t *pkts_gen, uint32_t *tx_wait) {
    if (pkts_gen) *pkts_gen = media_tracker.stat_packets_generated;
    if (tx_wait)  *tx_wait  = media_tracker.stat_tx_wait_count;
}
