#ifndef OEM_VOTER_SETS_H
#define OEM_VOTER_SETS_H
/* no direct MMIO, RPMh grants, CPU PPM, or Windows boot hooks. All methods require one
 * external PASSIVE_LEVEL, non-reentrant serialization domain. */

#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define OEM_VOTESET_RESOURCES 32u /* two 16-command sleep/wake TCS images. */
#define OEM_SET_ACTIVE 0u
#define OEM_SET_SLEEP 1u
#define OEM_SET_WAKE 2u
#define OEM_SET_CLEAN 0u
#define OEM_SET_DIRTY 1u
#define OEM_SET_DIRTY_USE_ACTIVE 2u
#define OEM_SET_CLEAN_EXPLICIT 3u

typedef struct oem_voteset_io {
    void* ctx;
    uint32_t owner_epoch;
    int (*owns)(void*, uint32_t key);
    int (*read_current)(void*, uint32_t key, uint64_t* value);
    /* attests actual hardware/firmware transition completion for exact token; not simply the
     * presence of a status bit or cached ACK. */

    int (*resume_proof)(void*, uint64_t token);

    int (*active_proof)(void*, uint64_t token, uint32_t key, uint64_t value);
} oem_voteset_io_t;

typedef struct oem_local_vote {
    uint64_t data;
    uint8_t valid;
    uint8_t completion;
    uint8_t explicit; /* sticky explicit sleep/wake vote. */
    uint8_t dirty;
} oem_local_vote_t;

typedef struct oem_voteset_res {
    uint32_t key;
    oem_local_vote_t local[3];
    uint64_t at_rpmh;
    uint8_t remote_valid;
} oem_voteset_res_t;

typedef struct oem_voteset_state {
    oem_voteset_io_t io;
    oem_voteset_res_t res[OEM_VOTESET_RESOURCES];
    oem_voteset_res_t resume_snapshot[OEM_VOTESET_RESOURCES];
    uint64_t resume_ticket;
    uint32_t sequence;
    uint32_t revision;
    uint32_t snapshot_revision;
    uint32_t cnt;
    uint32_t sleep_mask;
    uint32_t wake_mask;
    uint8_t sealed;
    uint8_t closed;
    uint8_t faulted;
} oem_voteset_state_t;

int oem_vote_sets_init(oem_voteset_state_t*, const oem_voteset_io_t*);
/* add a res only after independent owner/readback verification. */
int oem_vote_sets_add(oem_voteset_state_t*, uint32_t key, uint64_t verified_active);
int oem_vote_sets_seal(oem_voteset_state_t*);
/* updates software local vote; does not submit hardware or claim completion. */

int oem_vote_sets_set(oem_voteset_state_t*, uint32_t key, uint8_t set, uint8_t valid,
                      uint64_t value, uint8_t completion, uint8_t explicit_vote);

int oem_vote_sets_refresh(oem_voteset_state_t*, uint8_t check_completion);

int oem_vote_sets_command(const oem_voteset_state_t*, uint8_t set, uint32_t idx, uint32_t* key,
                          uint64_t* value);
/* update remote state only after authenticated exact active ticket and independent stable
 * provider readback of the new active state. */

int oem_vote_sets_commit_active(oem_voteset_state_t*, uint32_t key, uint64_t token);
/* a firmware resume proof and independent readbacks are required to promote valid wake
 * votes to active. failure quarantines the entire epoch. */

int oem_vote_sets_arm_resume(oem_voteset_state_t*, uint64_t* token);
int oem_vote_sets_commit_resume(oem_voteset_state_t*, uint64_t token);
/* cancellation allowed only before firmware/driver may have entered resume. Host must
 * ensure no side-effect could have started. */

int oem_vote_sets_cancel_unstarted(oem_voteset_state_t*, uint64_t token);
void oem_vote_sets_close(oem_voteset_state_t*);
int oem_vote_sets_can_release(const oem_voteset_state_t*);
#ifdef __cplusplus
}
#endif
#endif
