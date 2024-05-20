#
# Copyright (C) 2024 The LineageOS Project
#
# SPDX-License-Identifier: Apache-2.0
#

$(call inherit-product, device/meizu/mz845/mz845.mk)

# Inherit some common Lineage stuff.
$(call inherit-product, vendor/lineage/config/common_full_phone.mk)

# Device identifier. This must come after all inclusions.
PRODUCT_NAME := lineage_mz845
PRODUCT_DEVICE := mz845
PRODUCT_BRAND := Meizu
PRODUCT_MODEL := 16th
PRODUCT_MANUFACTURER := Meizu

PRODUCT_SYSTEM_NAME := meizu_16th_CN

BUILD_FINGERPRINT := "Meizu/meizu_16th_CN/16th:8.1.0/OPM1.171019.026/1594833800:user/release-keys"

PRODUCT_BUILD_PROP_OVERRIDES += \
    PRIVATE_BUILD_DESC="meizu-user meizu_16th_CN 16th:8.1.0 OPM1.171019.026 1619184508:user release-keys" \
    TARGET_PRODUCT="mz845"

PRODUCT_GMS_CLIENTID_BASE := android-meizu
