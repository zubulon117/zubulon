// main/sp_record.h —— A/B 存档记录的定长二进制编校（纯逻辑，不接触 NVS）。
//
// 记录布局（小端、定长 64 字节）：
//   偏移 0  magic       u32  固定 0x535031 ('SP1')
//   偏移 4  version     u16  当前 1
//   偏移 6  payload_len u16  负载实际字节数（≤ SP_RECORD_PAYLOAD）
//   偏移 8  sequence    u32  单调递增序号（A/B 选新用）
//   偏移 12 payload     48B  sp_pet_t 的可移植序列化
//   偏移 60 crc32       u32  对前 60 字节的 CRC32（IEEE 802.3）
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "sp_model.h"

#define SP_RECORD_SIZE     64u
#define SP_RECORD_HEADER   12u
#define SP_RECORD_PAYLOAD  48u
#define SP_RECORD_MAGIC    0x535031u  // 'SP1'
#define SP_RECORD_VERSION  1u

typedef struct {
    uint8_t bytes[SP_RECORD_SIZE];
} sp_record_t;

// 由内存中的宠物状态编码一条记录（内部会先 sanitize）。
void sp_record_from_pet(const sp_pet_t *pet, uint32_t sequence,
                        sp_record_t *out);

// IEEE CRC32（记录校验所用多项式），供测试构造负例。
uint32_t sp_record_crc32(const uint8_t *data, size_t len);

// 校验整条记录：长度、magic、version、payload_len、CRC32。
bool sp_record_validate_buf(const uint8_t *raw, size_t len);
bool sp_record_validate(const sp_record_t *rec);

uint32_t sp_record_sequence(const sp_record_t *rec);

// 校验通过则解码到 pet（并做 sanitize）；失败返回 false 且不写 out。
bool sp_record_to_pet(const sp_record_t *rec, sp_pet_t *out);

// 在两个槽中选 sequence 更大的合法记录；非法槽用 NULL 表示。
// sequence 相等时优先 a。两槽都非法返回 NULL。
const sp_record_t *sp_record_newer(const sp_record_t *a,
                                   const sp_record_t *b);
