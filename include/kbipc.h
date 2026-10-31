#ifndef KBIPC_H
#define KBIPC_H

#include <stdint.h>

#define KBDD_IPC_MAX_PAYLOAD 65536u
#define KBDD_IPC_HEADER_SIZE 16u
#define KBDD_IPC_MAGIC 0x4B424444u
#define KBDD_IPC_VERSION 1u

#define KBDD_IPC_OP_PING 1u
#define KBDD_IPC_OP_GET_VERSION 2u
#define KBDD_IPC_OP_LIST_DEVICES 3u
#define KBDD_IPC_OP_SAVE_PROFILE 4u
#define KBDD_IPC_OP_WRITE_PROFILE 4u
#define KBDD_IPC_OP_STATUS 5u
#define KBDD_IPC_OP_READ_PROFILE 6u
#define KBDD_IPC_OP_READ_CONFIG 7u
#define KBDD_IPC_OP_ENABLE_NOTIFICATIONS 8u
#define KBDD_IPC_OP_READ_ACTIVE 9u   /* query 0x81: active profile (text reply) */
#define KBDD_IPC_OP_READ_BLOCK 10u   /* query 0x82: payload [profile]; 264 raw bytes */
#define KBDD_IPC_OP_LED_SET 11u      /* payload: see KBDD_LED_* below (7 bytes) */

/* LED_SET payload: [flags, stride, mode, pos, r, g, b] */
#define KBDD_LED_PAYLOAD_SIZE 7u
#define KBDD_LED_FLAG_HOLD 0x01u     /* keep re-sending the frame every 40 ms */
#define KBDD_LED_MODE_SOLID 0u       /* all matrix positions = (r,g,b) */
#define KBDD_LED_MODE_POS 1u         /* only matrix position `pos` = (r,g,b) */
#define KBDD_LED_MODE_RELEASE 2u     /* stop streaming (no frame sent) */

#endif /* KBIPC_H */