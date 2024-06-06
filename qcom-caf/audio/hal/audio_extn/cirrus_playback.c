/*
 * Copyright (C) 2017 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#define LOG_TAG "audio_hw_cirrus_playback"
/*#define LOG_NDEBUG 0*/

#include <errno.h>
#include <math.h>
#include <log/log.h>
#include <fcntl.h>
#include "../audio_hw.h"
#include "platform.h"
#include "platform_api.h"
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/ioctl.h>
#include <stdlib.h>
#include <stdio.h>
#include <dlfcn.h>
#include <math.h>
#include <pthread.h>
#include <time.h>
#include <unistd.h>
#include <cutils/properties.h>
#include "audio_extn.h"

// - external function dependency -
static fp_platform_get_snd_device_name_t fp_platform_get_snd_device_name;
static fp_platform_get_pcm_device_id_t fp_platform_get_pcm_device_id;
static fp_get_usecase_from_list_t fp_get_usecase_from_list;
static fp_enable_disable_snd_device_t fp_disable_snd_device;
static fp_enable_disable_snd_device_t  fp_enable_snd_device;
static fp_enable_disable_audio_route_t fp_disable_audio_route;
static fp_enable_disable_audio_route_t fp_enable_audio_route;
static fp_audio_extn_get_snd_card_split_t fp_audio_extn_get_snd_card_split;

struct cirrus_playback_session {
    void *adev_handle;
    pthread_mutex_t fb_prot_mutex;
    struct pcm *pcm_tx;
};

struct pcm_config pcm_config_cirrus_tx = {
    .channels = 4,
    .rate = 48000,
    .period_size = 256,
    .period_count = 4,
    .format = PCM_FORMAT_S16_LE,
    .start_threshold = 0,
    .stop_threshold = INT_MAX,
    .avail_min = 0,
};

static struct cirrus_playback_session handle;

void spkr_prot_init(void *adev, spkr_prot_init_config_t spkr_prot_init_config_val) {
    ALOGI("%s: Initialize Cirrus Logic Playback module", __func__);

    memset(&handle, 0, sizeof(handle));
    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return;
    }

    handle.adev_handle = adev;

    // init function pointers
    fp_platform_get_snd_device_name = spkr_prot_init_config_val.fp_platform_get_snd_device_name;
    fp_platform_get_pcm_device_id = spkr_prot_init_config_val.fp_platform_get_pcm_device_id;
    fp_get_usecase_from_list =  spkr_prot_init_config_val.fp_get_usecase_from_list;
    fp_disable_snd_device = spkr_prot_init_config_val.fp_disable_snd_device;
    fp_enable_snd_device = spkr_prot_init_config_val.fp_enable_snd_device;
    fp_disable_audio_route = spkr_prot_init_config_val.fp_disable_audio_route;
    fp_enable_audio_route = spkr_prot_init_config_val.fp_enable_audio_route;
    fp_audio_extn_get_snd_card_split = spkr_prot_init_config_val.fp_audio_extn_get_snd_card_split;

    pthread_mutex_init(&handle.fb_prot_mutex, NULL);
}

int spkr_prot_deinit() {
    ALOGV("%s: Entry", __func__);

    pthread_mutex_destroy(&handle.fb_prot_mutex);

    ALOGV("%s: Exit", __func__);
    return 0;
}

int spkr_prot_start_processing(snd_device_t snd_device) {
    struct audio_usecase *uc_info_tx;
    struct audio_device *adev = handle.adev_handle;
    int32_t pcm_dev_tx_id = -1, ret = 0;

    ALOGV("%s: Entry", __func__);

    if (!adev) {
        ALOGE("%s: Invalid params", __func__);
        return -EINVAL;
    }

    uc_info_tx = fp_get_usecase_from_list(adev, USECASE_AUDIO_CIRRUS_SPKR_CALIB_TX);
    if (uc_info_tx) {
        ALOGE("%s: Invalid state: usecase is already running!", __func__);
        return -EINVAL;
    }

    uc_info_tx = (struct audio_usecase *)calloc(1, sizeof(*uc_info_tx));
    if (!uc_info_tx) {
        ALOGE("%s: allocate memory failed", __func__);
        return -ENOMEM;
    }

    audio_route_apply_and_update_path(adev->audio_route,
                                      fp_platform_get_snd_device_name(snd_device));

    pthread_mutex_lock(&handle.fb_prot_mutex);
    uc_info_tx->id = USECASE_AUDIO_CIRRUS_SPKR_CALIB_TX;
    uc_info_tx->type = PCM_CAPTURE;
    uc_info_tx->in_snd_device = SND_DEVICE_IN_CAPTURE_CIRRUS_VI_FEEDBACK;
    uc_info_tx->out_snd_device = SND_DEVICE_NONE;
    list_init(&uc_info_tx->device_list);
    handle.pcm_tx = NULL;

    list_add_tail(&adev->usecase_list, &uc_info_tx->list);

    fp_enable_snd_device(adev, SND_DEVICE_IN_CAPTURE_CIRRUS_VI_FEEDBACK);
    fp_enable_audio_route(adev, uc_info_tx);

    pcm_dev_tx_id = fp_platform_get_pcm_device_id(uc_info_tx->id, PCM_CAPTURE);

    if (pcm_dev_tx_id < 0) {
        ALOGE("%s: Invalid pcm device for usecase (%d)",
              __func__, uc_info_tx->id);
        ret = -ENODEV;
        goto exit;
    }

    handle.pcm_tx = pcm_open(adev->snd_card,
                             pcm_dev_tx_id,
                             PCM_IN, &pcm_config_cirrus_tx);

    if (handle.pcm_tx && !pcm_is_ready(handle.pcm_tx)) {
        ALOGE("%s: PCM device not ready: %s", __func__, pcm_get_error(handle.pcm_tx));
        ret = -EIO;
        goto exit;
    }

    if (pcm_start(handle.pcm_tx) < 0) {
        ALOGE("%s: pcm start for TX failed; error = %s", __func__,
              pcm_get_error(handle.pcm_tx));
        ret = -EINVAL;
        goto exit;
    }

exit:
    if (ret) {
        if (handle.pcm_tx) {
            ALOGI("%s: pcm_tx_close", __func__);
            pcm_close(handle.pcm_tx);
            handle.pcm_tx = NULL;
        }

        fp_disable_audio_route(adev, uc_info_tx);
        fp_disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_CIRRUS_VI_FEEDBACK);
        list_remove(&uc_info_tx->list);
        free(uc_info_tx);
    }

    pthread_mutex_unlock(&handle.fb_prot_mutex);
    ALOGV("%s: Exit", __func__);
    return ret;
}

void spkr_prot_stop_processing(snd_device_t snd_device) {
    struct audio_usecase *uc_info_tx;
    struct audio_device *adev = handle.adev_handle;

    ALOGV("%s: Entry", __func__);

    pthread_mutex_lock(&handle.fb_prot_mutex);

    uc_info_tx = fp_get_usecase_from_list(adev, USECASE_AUDIO_CIRRUS_SPKR_CALIB_TX);
    if (uc_info_tx) {
        if (handle.pcm_tx) {
            ALOGI("%s: pcm_tx_close", __func__);
            pcm_close(handle.pcm_tx);
            handle.pcm_tx = NULL;
        }

        fp_disable_audio_route(adev, uc_info_tx);
        fp_disable_snd_device(adev, SND_DEVICE_IN_CAPTURE_CIRRUS_VI_FEEDBACK);
        list_remove(&uc_info_tx->list);
        free(uc_info_tx);

        audio_route_reset_path(adev->audio_route,
                               fp_platform_get_snd_device_name(snd_device));
    }

    pthread_mutex_unlock(&handle.fb_prot_mutex);

    ALOGV("%s: Exit", __func__);
}

bool spkr_prot_is_enabled() {
    return true;
}

int get_spkr_prot_snd_device(snd_device_t snd_device) {
    switch(snd_device) {
    case SND_DEVICE_OUT_SPEAKER:
    case SND_DEVICE_OUT_SPEAKER_REVERSE:
        return SND_DEVICE_OUT_SPEAKER_PROTECTED;
    case SND_DEVICE_OUT_SPEAKER_SAFE:
        return SND_DEVICE_OUT_SPEAKER_SAFE;
    case SND_DEVICE_OUT_VOICE_SPEAKER:
        return SND_DEVICE_OUT_VOICE_SPEAKER_PROTECTED;
    default:
        return snd_device;
    }
}

void spkr_prot_calib_cancel(__unused void *adev) {
    // FIXME: wait or cancel audio_extn_cirrus_run_calibration
}
