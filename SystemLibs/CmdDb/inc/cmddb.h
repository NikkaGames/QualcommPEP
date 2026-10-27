#ifndef CMDDB_H
#define CMDDB_H
/* the caller must map the shared DB read-only from the firmware-provided address; this
 * parser neither trusts nor dereferences the firmware pointer. */

#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#define CMDDB_MAGIC 0x0c0330dbu
#define CMDDB_VERSION 1u

#define CMDDB_MAX_BYTES 0x20000u
#define CMDDB_PHYS UINT64_C(0x81c60000)
#define CMDDB_HEADER_BYTES 144u
#define CMDDB_ENTRY_BYTES 24u
#define CMDDB_SLV_CNT 8u

typedef struct cmddb_entry {
    uint32_t address;
    uint32_t priority[2];
    uint16_t version;
    const uint8_t* aux_data;
    uint16_t aux_length;
} cmddb_entry_t;

/* parses a bounded, untrusted binary. Name must be ASCII (1..8 bytes). */

int cmd_db_lookup(const uint8_t* database, size_t bytes, const char* name, cmddb_entry_t* out);
#ifdef __cplusplus
}
#endif
#endif
