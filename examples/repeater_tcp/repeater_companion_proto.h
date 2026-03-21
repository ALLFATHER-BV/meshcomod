#pragma once

#include <stdint.h>

/* Subset of meshcomod companion framing. */
#define REPEATER_COMPANION_FIRMWARE_VER_CODE 26

#define CMD_APP_START                 1
#define CMD_SEND_TXT_MSG              2

#define RESP_CODE_SENT                6
#define RESP_CODE_CONTACT_MSG_RECV_V3 16

#define PUSH_CODE_SEND_CONFIRMED      0x82
#define PUSH_CODE_MSG_WAITING         0x83
#define PUSH_CODE_BINARY_RESPONSE       0x8C
#define CMD_GET_CONTACTS              4
#define CMD_GET_DEVICE_TIME           5
#define CMD_SET_DEVICE_TIME           6
#define CMD_SEND_SELF_ADVERT          7
#define CMD_SET_ADVERT_NAME           8
#define CMD_SYNC_NEXT_MESSAGE         10
#define CMD_SET_RADIO_PARAMS          11
#define CMD_SET_RADIO_TX_POWER        12
#define CMD_SET_ADVERT_LATLON         14
#define CMD_SET_OTHER_PARAMS          38
#define CMD_REBOOT                    19
#define CMD_GET_BATT_AND_STORAGE      20
#define CMD_SET_TUNING_PARAMS         21
#define CMD_DEVICE_QUERY              22
#define CMD_EXPORT_PRIVATE_KEY        23
#define CMD_IMPORT_PRIVATE_KEY        24
#define CMD_GET_TUNING_PARAMS         43
#define CMD_FACTORY_RESET             51
#define CMD_GET_STATS                 56
#define CMD_SET_PATH_HASH_MODE        61
#define CMD_SYNC_SINCE                62

#define STATS_TYPE_CORE               0
#define STATS_TYPE_RADIO              1
#define STATS_TYPE_PACKETS            2

#define RESP_CODE_OK                  0
#define RESP_CODE_ERR                 1
#define RESP_CODE_CONTACTS_START      2
#define RESP_CODE_END_OF_CONTACTS     4
#define RESP_CODE_SELF_INFO           5
#define RESP_CODE_CURR_TIME           9
#define RESP_CODE_BATT_AND_STORAGE    12
#define RESP_CODE_DEVICE_INFO         13
#define RESP_CODE_PRIVATE_KEY         14
#define RESP_CODE_DISABLED            15
#define RESP_CODE_NO_MORE_MESSAGES    10
#define RESP_CODE_TUNING_PARAMS       23
#define RESP_CODE_STATS               24
#define RESP_CODE_SYNC_SINCE_DONE     61

#define ERR_CODE_UNSUPPORTED_CMD      1
#define ERR_CODE_NOT_FOUND            2
#define ERR_CODE_TABLE_FULL           3
#define ERR_CODE_BAD_STATE            4
#define ERR_CODE_FILE_IO_ERROR        5
#define ERR_CODE_ILLEGAL_ARG          6
