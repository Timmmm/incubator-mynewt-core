/**
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *  http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 */

#include <string.h>
#include <errno.h>
#include "nimble/ble.h"
#include "host/ble_hs_adv.h"
#include "ble_hs_priv.h"

static uint16_t ble_hs_adv_uuids16[BLE_HS_ADV_MAX_FIELD_SZ / 2];
static uint32_t ble_hs_adv_uuids32[BLE_HS_ADV_MAX_FIELD_SZ / 4];

static int
ble_hs_adv_set_hdr(uint8_t type, uint8_t data_len, uint8_t max_len,
                   uint8_t *dst, uint8_t *dst_len)
{
    if (*dst_len + 2 + data_len > max_len) {
        return BLE_HS_EMSGSIZE;
    }

    dst[*dst_len] = data_len + 1;
    dst[*dst_len + 1] = type;

    *dst_len += 2;

    return 0;
}

int
ble_hs_adv_set_flat(uint8_t type, int data_len, const void *data,
                    uint8_t *dst, uint8_t *dst_len, uint8_t max_len)
{
#if !NIMBLE_OPT(ADVERTISE)
    return BLE_HS_ENOTSUP;
#endif

    int rc;

    BLE_HS_DBG_ASSERT(data_len > 0);

    rc = ble_hs_adv_set_hdr(type, data_len, max_len, dst, dst_len);
    if (rc != 0) {
        return rc;
    }

    memcpy(dst + *dst_len, data, data_len);
    *dst_len += data_len;

    return 0;
}

static int
ble_hs_adv_set_array16(uint8_t type, uint8_t num_elems, const uint16_t *elems,
                       uint8_t *dst, uint8_t *dst_len, uint8_t max_len)
{
    int rc;
    int i;

    rc = ble_hs_adv_set_hdr(type, num_elems * sizeof *elems, max_len, dst,
                            dst_len);
    if (rc != 0) {
        return rc;
    }

    for (i = 0; i < num_elems; i++) {
        htole16(dst + *dst_len, elems[i]);
        *dst_len += sizeof elems[i];
    }

    return 0;
}

static int
ble_hs_adv_set_array32(uint8_t type, uint8_t num_elems, const uint32_t *elems,
                       uint8_t *dst, uint8_t *dst_len, uint8_t max_len)
{
    int rc;
    int i;

    rc = ble_hs_adv_set_hdr(type, num_elems * sizeof *elems, max_len, dst,
                            dst_len);
    if (rc != 0) {
        return rc;
    }

    for (i = 0; i < num_elems; i++) {
        htole32(dst + *dst_len, elems[i]);
        *dst_len += sizeof elems[i];
    }

    return 0;
}

/**
 * Sets the significant part of the data in outgoing advertisements.
 *
 * @return                      0 on success; nonzero on failure.
 */
int
ble_hs_adv_set_fields(const struct ble_hs_adv_fields *adv_fields,
                      uint8_t *dst, uint8_t *dst_len, uint8_t max_len)
{
#if !NIMBLE_OPT(ADVERTISE)
    return BLE_HS_ENOTSUP;
#endif

    uint8_t type;
    int8_t tx_pwr_lvl;
    int rc;

    *dst_len = 0;

    /*** 0x01 - Flags. */
    /* The application has three options concerning the flags field:
     * 1. Don't include it in advertisements (!flags_is_present).
     * 2. Explicitly specify the value (flags_is_present && flags != 0).
     * 3. Let stack calculate the value (flags_is_present && flags == 0).
     *
     * For option 3, the calculation is delayed until advertising is enabled.
     * The delay is necessary because the flags value depends on the type of
     * advertising being performed which is not known at this time.
     *
     * Note: The CSS prohibits advertising a flags value of 0, so this method
     * of specifying option 2 vs. 3 is sound.
     */
    if (adv_fields->flags_is_present && adv_fields->flags != 0) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_FLAGS, 1, &adv_fields->flags,
                                 dst, dst_len, max_len);
    }

    /*** 0x02,0x03 - 16-bit service class UUIDs. */
    if (adv_fields->uuids16 != NULL && adv_fields->num_uuids16) {
        if (adv_fields->uuids16_is_complete) {
            type = BLE_HS_ADV_TYPE_COMP_UUIDS16;
        } else {
            type = BLE_HS_ADV_TYPE_INCOMP_UUIDS16;
        }

        rc = ble_hs_adv_set_array16(type, adv_fields->num_uuids16,
                                    adv_fields->uuids16, dst, dst_len,
                                    max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x04,0x05 - 32-bit service class UUIDs. */
    if (adv_fields->uuids32 != NULL && adv_fields->num_uuids32) {
        if (adv_fields->uuids32_is_complete) {
            type = BLE_HS_ADV_TYPE_COMP_UUIDS32;
        } else {
            type = BLE_HS_ADV_TYPE_INCOMP_UUIDS32;
        }

        rc = ble_hs_adv_set_array32(type, adv_fields->num_uuids32,
                                    adv_fields->uuids32, dst, dst_len,
                                    max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x06,0x07 - 128-bit service class UUIDs. */
    if (adv_fields->uuids128 != NULL && adv_fields->num_uuids128 > 0) {
        if (adv_fields->uuids128_is_complete) {
            type = BLE_HS_ADV_TYPE_COMP_UUIDS128;
        } else {
            type = BLE_HS_ADV_TYPE_INCOMP_UUIDS128;
        }

        rc = ble_hs_adv_set_flat(type, adv_fields->num_uuids128 * 16,
                                 adv_fields->uuids128, dst, dst_len, max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x08,0x09 - Local name. */
    if (adv_fields->name != NULL && adv_fields->name_len > 0) {
        if (adv_fields->name_is_complete) {
            type = BLE_HS_ADV_TYPE_COMP_NAME;
        } else {
            type = BLE_HS_ADV_TYPE_INCOMP_NAME;
        }

        rc = ble_hs_adv_set_flat(type, adv_fields->name_len, adv_fields->name,
                                 dst, dst_len, max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x0a - Tx power level. */
    if (adv_fields->tx_pwr_lvl_is_present) {
        /* Read the power level from the controller if requested; otherwise use
         * the explicitly specified value.
         */
        if (adv_fields->tx_pwr_lvl == BLE_HS_ADV_TX_PWR_LVL_AUTO) {
            rc = ble_hs_hci_util_read_adv_tx_pwr(&tx_pwr_lvl);
            if (rc != 0) {
                return rc;
            }
        } else {
            tx_pwr_lvl = adv_fields->tx_pwr_lvl;
        }

        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_TX_PWR_LVL, 1, &tx_pwr_lvl,
                                 dst, dst_len, max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x0d - Class of device. */
    if (adv_fields->device_class != NULL) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_DEVICE_CLASS,
                                 BLE_HS_ADV_DEVICE_CLASS_LEN,
                                 adv_fields->device_class, dst, dst_len,
                                 max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x12 - Slave connection interval range. */
    if (adv_fields->slave_itvl_range != NULL) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_SLAVE_ITVL_RANGE,
                                 BLE_HS_ADV_SLAVE_ITVL_RANGE_LEN,
                                 adv_fields->slave_itvl_range, dst, dst_len,
                                 max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x16 - Service data - 16-bit UUID. */
    if (adv_fields->svc_data_uuid16 != NULL) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_SVC_DATA_UUID16,
                                 adv_fields->svc_data_uuid16_len,
                                 adv_fields->svc_data_uuid16, dst, dst_len,
                                 max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x17 - Public target address. */
    if (adv_fields->public_tgt_addr != NULL &&
        adv_fields->num_public_tgt_addrs != 0) {

        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_PUBLIC_TGT_ADDR,
                                 BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN *
                                     adv_fields->num_public_tgt_addrs,
                                 adv_fields->public_tgt_addr, dst, dst_len,
                                 max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x19 - Appearance. */
    if (adv_fields->appearance_is_present) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_APPEARANCE,
                                 BLE_HS_ADV_APPEARANCE_LEN,
                                 &adv_fields->appearance, dst, dst_len,
                                 max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x1a - Advertising interval. */
    if (adv_fields->adv_itvl_is_present) {
        rc = ble_hs_adv_set_array16(BLE_HS_ADV_TYPE_ADV_ITVL, 1,
                                    &adv_fields->adv_itvl, dst, dst_len,
                                    max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x1b - LE bluetooth device address. */
    if (adv_fields->le_addr != NULL) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_LE_ADDR,
                                 BLE_HS_ADV_LE_ADDR_LEN,
                                 adv_fields->le_addr, dst, dst_len,
                                 max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x1c - LE role. */
    if (adv_fields->le_role_is_present) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_LE_ROLE,
                                 BLE_HS_ADV_LE_ROLE_LEN,
                                 &adv_fields->le_role, dst, dst_len, max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x20 - Service data - 32-bit UUID. */
    if (adv_fields->svc_data_uuid32 != NULL) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_SVC_DATA_UUID32,
                                 adv_fields->svc_data_uuid32_len,
                                 adv_fields->svc_data_uuid32, dst, dst_len,
                                 max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x21 - Service data - 128-bit UUID. */
    if (adv_fields->svc_data_uuid128 != NULL) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_SVC_DATA_UUID128,
                                 adv_fields->svc_data_uuid128_len,
                                 adv_fields->svc_data_uuid128, dst, dst_len,
                                 max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0x24 - URI. */
    if (adv_fields->uri != NULL) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_URI, adv_fields->uri_len,
                                 adv_fields->uri, dst, dst_len, max_len);
        if (rc != 0) {
            return rc;
        }
    }

    /*** 0xff - Manufacturer specific data. */
    if (adv_fields->mfg_data != NULL) {
        rc = ble_hs_adv_set_flat(BLE_HS_ADV_TYPE_MFG_DATA,
                                 adv_fields->mfg_data_len,
                                 adv_fields->mfg_data, dst, dst_len, max_len);
        if (rc != 0) {
            return rc;
        }
    }

    return 0;
}

static int
ble_hs_adv_parse_uuids16(struct ble_hs_adv_fields *adv_fields,
                         const uint8_t *data, uint8_t data_len)
{
    int i;

    if (data_len % 2 != 0) {
        return BLE_HS_EBADDATA;
    }

    adv_fields->uuids16 = ble_hs_adv_uuids16;
    adv_fields->num_uuids16 = data_len / 2;

    for (i = 0; i < adv_fields->num_uuids16; i++) {
        adv_fields->uuids16[i] = le16toh(data + i * 2);
    }

    return 0;
}

static int
ble_hs_adv_parse_uuids32(struct ble_hs_adv_fields *adv_fields,
                         const uint8_t *data, uint8_t data_len)
{
    int i;

    if (data_len % 4 != 0) {
        return BLE_HS_EBADDATA;
    }

    adv_fields->uuids32 = ble_hs_adv_uuids32;
    adv_fields->num_uuids32 = data_len / 4;

    for (i = 0; i < adv_fields->num_uuids32; i++) {
        adv_fields->uuids32[i] = le32toh(data + i * 4);
    }

    return 0;
}

static int
ble_hs_adv_parse_one_field(struct ble_hs_adv_fields *adv_fields,
                           uint8_t *total_len, uint8_t *src, uint8_t src_len)
{
    uint8_t data_len;
    uint8_t type;
    uint8_t *data;
    int rc;

    if (src_len < 1) {
        return BLE_HS_EMSGSIZE;
    }
    *total_len = src[0] + 1;

    if (src_len < *total_len) {
        return BLE_HS_EMSGSIZE;
    }

    type = src[1];
    data = src + 2;
    data_len = *total_len - 2;

    if (data_len > BLE_HS_ADV_MAX_FIELD_SZ) {
        return BLE_HS_EBADDATA;
    }

    switch (type) {
    case BLE_HS_ADV_TYPE_FLAGS:
        if (data_len != BLE_HS_ADV_FLAGS_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->flags = *data;
        adv_fields->flags_is_present = 1;
        break;

    case BLE_HS_ADV_TYPE_INCOMP_UUIDS16:
        rc = ble_hs_adv_parse_uuids16(adv_fields, data, data_len);
        if (rc != 0) {
            return rc;
        }
        adv_fields->uuids16_is_complete = 0;
        break;

    case BLE_HS_ADV_TYPE_COMP_UUIDS16:
        rc = ble_hs_adv_parse_uuids16(adv_fields, data, data_len);
        if (rc != 0) {
            return rc;
        }
        adv_fields->uuids16_is_complete = 1;
        break;

    case BLE_HS_ADV_TYPE_INCOMP_UUIDS32:
        rc = ble_hs_adv_parse_uuids32(adv_fields, data, data_len);
        if (rc != 0) {
            return rc;
        }
        adv_fields->uuids16_is_complete = 0;
        break;

    case BLE_HS_ADV_TYPE_COMP_UUIDS32:
        rc = ble_hs_adv_parse_uuids32(adv_fields, data, data_len);
        if (rc != 0) {
            return rc;
        }
        adv_fields->uuids16_is_complete = 1;
        break;

    case BLE_HS_ADV_TYPE_INCOMP_UUIDS128:
        if (data_len % 16 != 0) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->uuids128 = data;
        adv_fields->num_uuids128 = data_len / 16;
        adv_fields->uuids128_is_complete = 0;
        break;

    case BLE_HS_ADV_TYPE_COMP_UUIDS128:
        if (data_len % 16 != 0) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->uuids128 = data;
        adv_fields->num_uuids128 = data_len / 16;
        adv_fields->uuids128_is_complete = 1;
        break;

    case BLE_HS_ADV_TYPE_INCOMP_NAME:
        adv_fields->name = data;
        adv_fields->name_len = data_len;
        adv_fields->name_is_complete = 0;
        break;

    case BLE_HS_ADV_TYPE_COMP_NAME:
        adv_fields->name = data;
        adv_fields->name_len = data_len;
        adv_fields->name_is_complete = 1;
        break;

    case BLE_HS_ADV_TYPE_TX_PWR_LVL:
        if (data_len != BLE_HS_ADV_TX_PWR_LVL_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->tx_pwr_lvl = *data;
        adv_fields->tx_pwr_lvl_is_present = 1;
        break;

    case BLE_HS_ADV_TYPE_DEVICE_CLASS:
        if (data_len != BLE_HS_ADV_DEVICE_CLASS_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->device_class = data;
        break;

    case BLE_HS_ADV_TYPE_SLAVE_ITVL_RANGE:
        if (data_len != BLE_HS_ADV_SLAVE_ITVL_RANGE_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->slave_itvl_range = data;
        break;

    case BLE_HS_ADV_TYPE_SVC_DATA_UUID16:
        if (data_len < BLE_HS_ADV_SVC_DATA_UUID16_MIN_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->svc_data_uuid16 = data;
        adv_fields->svc_data_uuid16_len = data_len;
        break;

    case BLE_HS_ADV_TYPE_PUBLIC_TGT_ADDR:
        if (data_len % BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN != 0) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->public_tgt_addr = data;
        adv_fields->num_public_tgt_addrs =
            data_len / BLE_HS_ADV_PUBLIC_TGT_ADDR_ENTRY_LEN;
        break;

    case BLE_HS_ADV_TYPE_APPEARANCE:
        if (data_len != BLE_HS_ADV_APPEARANCE_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->appearance = le16toh(data);
        adv_fields->appearance_is_present = 1;
        break;

    case BLE_HS_ADV_TYPE_ADV_ITVL:
        if (data_len != BLE_HS_ADV_ADV_ITVL_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->adv_itvl = le16toh(data);
        adv_fields->adv_itvl_is_present = 1;
        break;

    case BLE_HS_ADV_TYPE_LE_ADDR:
        if (data_len != BLE_HS_ADV_LE_ADDR_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->le_addr = data;
        break;

    case BLE_HS_ADV_TYPE_LE_ROLE:
        if (data_len != BLE_HS_ADV_LE_ROLE_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->le_role = *data;
        adv_fields->le_role_is_present = 1;
        break;

    case BLE_HS_ADV_TYPE_SVC_DATA_UUID32:
        if (data_len < BLE_HS_ADV_SVC_DATA_UUID32_MIN_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->svc_data_uuid32 = data;
        adv_fields->svc_data_uuid32_len = data_len;
        break;

    case BLE_HS_ADV_TYPE_SVC_DATA_UUID128:
        if (data_len < BLE_HS_ADV_SVC_DATA_UUID128_MIN_LEN) {
            return BLE_HS_EBADDATA;
        }
        adv_fields->svc_data_uuid128 = data;
        adv_fields->svc_data_uuid128_len = data_len;
        break;

    case BLE_HS_ADV_TYPE_URI:
        adv_fields->uri = data;
        adv_fields->uri_len = data_len;
        break;

    case BLE_HS_ADV_TYPE_MFG_DATA:
        adv_fields->mfg_data = data;
        adv_fields->mfg_data_len = data_len;
        break;

    default:
        break;
    }

    return 0;
}

int
ble_hs_adv_parse_fields(struct ble_hs_adv_fields *adv_fields, uint8_t *src,
                        uint8_t src_len)
{
    uint8_t field_len;
    int rc;

    memset(adv_fields, 0, sizeof *adv_fields);

    while (src_len > 0) {
        rc = ble_hs_adv_parse_one_field(adv_fields, &field_len, src, src_len);
        if (rc != 0) {
            return rc;
        }

        src += field_len;
        src_len -= field_len;
    }

    return 0;
}
