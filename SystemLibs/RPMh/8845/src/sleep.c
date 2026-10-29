#include "sleep.h"
#include <string.h>

#define SLEEP_BCM_COMMIT (1u << 30)

static int result(rpmh_sleep_t* owner, rpmh_sleep_result_t value)
{
    if (owner) {
        owner->last_result = value;
        if (value != RPMH_SLEEP_OK)
            ++owner->failures;
    }
    return value == RPMH_SLEEP_OK;
}

static uint32_t bank_count(const rpmh_sleep_t* owner)
{
    return owner->layout.sleep_count + owner->layout.wake_count;
}

static uint32_t bank_slot(const rpmh_sleep_t* owner, uint32_t bank)
{
    return bank < owner->layout.sleep_count
               ? owner->layout.sleep_first + bank
               : owner->layout.wake_first + bank - owner->layout.sleep_count;
}

static uint32_t bank_mask(const rpmh_sleep_t* owner)
{
    uint32_t mask = 0u;
    for (uint32_t i = 0u; i < bank_count(owner); ++i)
        mask |= 1u << bank_slot(owner, i);
    return mask;
}

static int authorized(const rpmh_sleep_t* owner)
{
    return owner && owner->epoch && owner->generation && owner->io.authorized &&
           owner->io.authorized(owner->io.ctx, owner->epoch, owner->generation);
}

static int idle(rpmh_sleep_t* owner)
{
    uint32_t value;
    rsc_hlos_geometry_t geometry;
    if (!authorized(owner) || !owner->io.quiescent(owner->io.ctx) ||
        !owner->io.read(owner->io.ctx, RSC_ID_OFFSET, &value) ||
        (value & RSC_ID_MASK) != RSC_ID_POR ||
        !owner->io.read(owner->io.ctx, RSC_PARENTCHILD_CONFIG_OFFSET, &value) ||
        !rpmh_tcs_check_hlos_geometry(value, owner->layout.total_tcs, owner->layout.commands,
                                      &geometry) ||
        geometry.tcs_cnt != owner->layout.total_tcs ||
        geometry.commands_per_tcs != owner->layout.commands ||
        (owner->bound && value != owner->config))
        return 0;
    owner->config = value;
    if (!owner->io.read(owner->io.ctx, TCS_IRQ_STATUS, &value) || value ||
        !owner->io.read(owner->io.ctx, TCS_IRQ_ENABLE, &value) || (value & bank_mask(owner)))
        return 0;
    for (uint32_t i = 0u; i < bank_count(owner); ++i) {
        uint32_t slot = bank_slot(owner, i);
        if (!owner->io.read(owner->io.ctx, TCS_STATUS(slot), &value) || value == UINT32_MAX ||
            !(value & TCS_STATUS_IDLE) ||
            !owner->io.read(owner->io.ctx, TCS_CONTROL(slot), &value) || value == UINT32_MAX ||
            (value & (TCS_CONTROL_AMC_MODE_EN | TCS_CONTROL_AMC_TRIGGER)))
            return 0;
    }
    return authorized(owner);
}

int rpmh_sleep_init(rpmh_sleep_t* owner, const rpmh_sleep_io_t* io,
                    const rpmh_sleep_layout_t* layout, uint64_t epoch, uint32_t generation)
{
    rpmh_sleep_io_t binding;
    rpmh_sleep_layout_t allocation;
    uint32_t mask = 0u;
    if (!owner || !io || !layout || !io->read || !io->write || !io->barrier || !io->authorized ||
        !io->quiescent || !epoch || !generation || !layout->total_tcs || layout->total_tcs > 8u ||
        !layout->sleep_count || !layout->wake_count ||
        layout->sleep_count + layout->wake_count > RPMH_SLEEP_BANKS ||
        layout->commands != TCS_BATCH_MAX || !layout->active_mask ||
        (layout->active_mask >> layout->total_tcs) ||
        layout->sleep_first + layout->sleep_count > layout->total_tcs ||
        layout->wake_first + layout->wake_count > layout->total_tcs || owner->bound ||
        owner->frozen || owner->faulted)
        return 0;
    for (uint32_t i = 0u; i < (uint32_t)layout->sleep_count + layout->wake_count; ++i) {
        uint32_t slot = i < layout->sleep_count ? layout->sleep_first + i
                                                : layout->wake_first + i - layout->sleep_count;
        if ((mask | layout->active_mask) & (1u << slot))
            return 0;
        mask |= 1u << slot;
    }
    binding = *io;
    allocation = *layout;
    memset(owner, 0, sizeof(*owner));
    owner->io = binding;
    owner->layout = allocation;
    owner->epoch = epoch;
    owner->generation = generation;
    owner->revision = 1u;
    return 1;
}

static rpmh_sleep_resource_t* find_resource(rpmh_sleep_t* owner, uint32_t address)
{
    for (uint32_t i = 0u; i < owner->resource_count; ++i)
        if (owner->resources[i].address == address)
            return &owner->resources[i];
    return NULL;
}

int rpmh_sleep_has_active(const rpmh_sleep_t* owner, uint32_t address)
{
    if (owner)
        for (uint32_t i = 0u; i < owner->resource_count; ++i)
            if (owner->resources[i].address == address)
                return 1;
    return 0;
}

int rpmh_sleep_record(rpmh_sleep_t* owner, const tcs_command_t* commands, uint32_t count)
{
    if (!authorized(owner) || !commands || !count || count > TCS_BATCH_MAX || owner->frozen ||
        owner->faulted || owner->cache_invalid)
        return 0;
    for (uint32_t i = 0u; i < count; ++i) {
        rpmh_sleep_resource_t* resource;
        if (commands[i].read)
            continue;
        if (!commands[i].address) {
            owner->cache_invalid = 1u;
            return 0;
        }
        resource = find_resource(owner, commands[i].address);
        if (!resource) {
            if (owner->resource_count == RPMH_SLEEP_RESOURCES) {
                owner->cache_invalid = 1u;
                return 0;
            }
            resource = &owner->resources[owner->resource_count++];
            resource->address = commands[i].address;
            ++owner->revision;
        }
        if (resource->active != commands[i].data)
            ++owner->revision;
        resource->active = commands[i].data;
    }
    return 1;
}

int rpmh_sleep_vote(rpmh_sleep_t* owner, uint32_t address, uint32_t sleep, int bcm, uint32_t vcd)
{
    rpmh_sleep_resource_t* resource;
    if (!authorized(owner) || owner->frozen || owner->faulted || owner->cache_invalid ||
        !(resource = find_resource(owner, address)))
        return 0;
    if (!resource->explicit_sleep || resource->sleep != sleep || resource->bcm != (bcm != 0) ||
        resource->vcd != vcd)
        ++owner->revision;
    resource->sleep = sleep;
    resource->bcm = bcm != 0;
    resource->vcd = vcd;
    resource->explicit_sleep = 1u;
    return 1;
}

static int read_bank(rpmh_sleep_t* owner, uint32_t index, rpmh_sleep_bank_t* bank)
{
    uint32_t slot = bank_slot(owner, index);
    if (!owner->io.read(owner->io.ctx, TCS_CONTROL(slot), &bank->control) ||
        !owner->io.read(owner->io.ctx, TCS_ENABLE(slot), &bank->enable) ||
        !owner->io.read(owner->io.ctx, TCS_WAIT(slot), &bank->wait) ||
        (bank->enable >> owner->layout.commands) || (bank->wait & ~bank->enable))
        return 0;
    for (uint32_t i = 0u; i < owner->layout.commands; ++i) {
        rpmh_sleep_command_t* command = &bank->commands[i];
        if (!owner->io.read(owner->io.ctx, TCS_CMD_MSGID(slot, i), &command->message) ||
            !owner->io.read(owner->io.ctx, TCS_CMD_ADDR(slot, i), &command->address) ||
            !owner->io.read(owner->io.ctx, TCS_CMD_DATA(slot, i), &command->data) ||
            ((bank->enable & (1u << i)) &&
             (!command->address || (command->message & 0xFFu) != TCS_MSGID_LENGTH)))
            return 0;
    }
    return 1;
}

static int verify(rpmh_sleep_t* owner, const rpmh_sleep_bank_t* image, int all_commands)
{
    uint32_t value;
    for (uint32_t i = 0u; i < bank_count(owner); ++i) {
        const rpmh_sleep_bank_t* bank = &image[i];
        uint32_t slot = bank_slot(owner, i);
        if (!owner->io.read(owner->io.ctx, TCS_CONTROL(slot), &value) || value != bank->control ||
            !owner->io.read(owner->io.ctx, TCS_ENABLE(slot), &value) || value != bank->enable ||
            !owner->io.read(owner->io.ctx, TCS_WAIT(slot), &value) || value != bank->wait)
            return 0;
        for (uint32_t j = 0u; j < owner->layout.commands; ++j) {
            const rpmh_sleep_command_t* command = &bank->commands[j];
            if (!all_commands && !((bank->enable | owner->touched[i]) & (1u << j)))
                continue;
            if (!owner->io.read(owner->io.ctx, TCS_CMD_MSGID(slot, j), &value) ||
                value != command->message ||
                !owner->io.read(owner->io.ctx, TCS_CMD_ADDR(slot, j), &value) ||
                value != command->address ||
                !owner->io.read(owner->io.ctx, TCS_CMD_DATA(slot, j), &value) ||
                value != command->data)
                return 0;
        }
    }
    return authorized(owner);
}

int rpmh_sleep_bind(rpmh_sleep_t* owner)
{
    if (!owner || owner->faulted || owner->frozen || owner->cache_invalid)
        return result(owner, RPMH_SLEEP_FAULT);
    if (!idle(owner))
        return result(owner, RPMH_SLEEP_BUSY);
    if (owner->bound)
        return result(owner,
                      verify(owner, owner->current, 0) ? RPMH_SLEEP_OK : RPMH_SLEEP_CONFLICT);
    for (uint32_t i = 0u; i < bank_count(owner); ++i)
        if (!read_bank(owner, i, &owner->baseline[i]))
            return result(owner, RPMH_SLEEP_IO);
    if (!idle(owner) || !verify(owner, owner->baseline, 1))
        return result(owner, RPMH_SLEEP_CONFLICT);
    memcpy(owner->current, owner->baseline, sizeof(owner->current));
    owner->bound = 1u;
    return result(owner, RPMH_SLEEP_OK);
}

/* Existing command order and dependencies belong to the inherited firmware image. */
static int replace(rpmh_sleep_t* owner, uint32_t first, uint32_t count,
                   const rpmh_sleep_resource_t* resource, uint32_t value, int* found)
{
    *found = 0;
    for (uint32_t i = first; i < first + count; ++i) {
        rpmh_sleep_bank_t* bank = &owner->next[i];
        for (uint32_t j = 0u; j < owner->layout.commands; ++j) {
            rpmh_sleep_command_t* command = &bank->commands[j];
            if (!(bank->enable & (1u << j)) || command->address != resource->address)
                continue;
            if (*found || !(command->message & TCS_MSGID_WRITE))
                return 0;
            command->data = resource->bcm
                                ? (value & ~SLEEP_BCM_COMMIT) | (command->data & SLEEP_BCM_COMMIT)
                                : value;
            *found = 1;
        }
    }
    return 1;
}

static int append(rpmh_sleep_t* owner, uint32_t first, uint32_t count,
                  const rpmh_sleep_resource_t* resource, uint32_t value, int wake)
{
    for (uint32_t i = first; i < first + count; ++i) {
        rpmh_sleep_bank_t* bank = &owner->next[i];
        uint32_t used = 0u;
        for (uint32_t j = 0u; j < owner->layout.commands; ++j)
            if (bank->enable & (1u << j))
                used = j + 1u;
        if (used == owner->layout.commands)
            continue;
        rpmh_sleep_command_t* command = &bank->commands[used];
        command->address = resource->address;
        command->data = resource->bcm ? value | SLEEP_BCM_COMMIT : value;
        command->message = TCS_MSGID_WRITE_CMD;
        bank->enable |= 1u << used;
        bank->wait |= 1u << used;
        /* Merge only newly appended commands in the same VCD and TCS. A TCS boundary
         * always ends with a response-bearing BCM commit. */
        if (resource->bcm && used && !(owner->baseline[i].enable & (1u << (used - 1u)))) {
            rpmh_sleep_command_t* prior = &bank->commands[used - 1u];
            rpmh_sleep_resource_t* previous = find_resource(owner, prior->address);
            if (previous && previous->bcm && previous->vcd == resource->vcd) {
                prior->data &= ~SLEEP_BCM_COMMIT;
                bank->wait &= ~(1u << (used - 1u));
                if (!wake)
                    prior->message &= ~TCS_MSGID_RES_REQ;
            }
        }
        return 1;
    }
    return 0;
}

static int before(const rpmh_sleep_resource_t* a, const rpmh_sleep_resource_t* b)
{
    if (a->bcm != b->bcm)
        return a->bcm < b->bcm;
    if (a->bcm && a->vcd != b->vcd)
        return a->vcd < b->vcd;
    /* Preserve issue order for rails and other resources with ordered writes. */
    return a->bcm && a->address < b->address;
}

static rpmh_sleep_result_t build(rpmh_sleep_t* owner)
{
    memcpy(owner->next, owner->baseline, sizeof(owner->next));
    for (uint32_t i = 0u; i < owner->resource_count; ++i) {
        uint32_t j = i;
        while (j && before(&owner->resources[i], &owner->resources[owner->order[j - 1u]])) {
            owner->order[j] = owner->order[j - 1u];
            --j;
        }
        owner->order[j] = (uint16_t)i;
    }
    for (uint32_t i = 0u; i < owner->resource_count; ++i) {
        const rpmh_sleep_resource_t* resource = &owner->resources[owner->order[i]];
        uint32_t sleep = resource->explicit_sleep ? resource->sleep : resource->active;
        uint32_t compare_mask = resource->bcm ? ~SLEEP_BCM_COMMIT : UINT32_MAX;
        int in_sleep, in_wake;
        if (!replace(owner, 0u, owner->layout.sleep_count, resource, sleep, &in_sleep) ||
            !replace(owner, owner->layout.sleep_count, owner->layout.wake_count, resource,
                     resource->active, &in_wake))
            return RPMH_SLEEP_CONFLICT;
        if ((sleep & compare_mask) == (resource->active & compare_mask))
            continue;
        if ((!in_sleep && !append(owner, 0u, owner->layout.sleep_count, resource, sleep, 0)) ||
            (!in_wake && !append(owner, owner->layout.sleep_count, owner->layout.wake_count,
                                 resource, resource->active, 1)))
            return RPMH_SLEEP_CAPACITY;
    }
    return RPMH_SLEEP_OK;
}

static int write_image(rpmh_sleep_t* owner, const rpmh_sleep_bank_t* image)
{
    uint32_t value;
    if (!idle(owner))
        return 0;
    for (uint32_t i = 0u; i < bank_count(owner); ++i) {
        uint32_t slot = bank_slot(owner, i);
        if (!owner->io.write(owner->io.ctx, TCS_ENABLE(slot), 0u) ||
            !owner->io.read(owner->io.ctx, TCS_ENABLE(slot), &value) || value)
            return 0;
    }
    owner->io.barrier(owner->io.ctx);
    for (uint32_t i = 0u; i < bank_count(owner); ++i) {
        uint32_t slot = bank_slot(owner, i);
        owner->touched[i] |= (uint16_t)(image[i].enable | owner->current[i].enable);
        if (!owner->io.write(owner->io.ctx, TCS_WAIT(slot), image[i].wait))
            return 0;
        for (uint32_t j = 0u; j < owner->layout.commands; ++j) {
            const rpmh_sleep_command_t* command = &image[i].commands[j];
            if (!(owner->touched[i] & (1u << j)))
                continue;
            if (!owner->io.write(owner->io.ctx, TCS_CMD_MSGID(slot, j), command->message) ||
                !owner->io.write(owner->io.ctx, TCS_CMD_ADDR(slot, j), command->address) ||
                !owner->io.write(owner->io.ctx, TCS_CMD_DATA(slot, j), command->data))
                return 0;
        }
    }
    owner->io.barrier(owner->io.ctx);
    if (!idle(owner))
        return 0;
    for (uint32_t i = 0u; i < bank_count(owner); ++i)
        if (!owner->io.write(owner->io.ctx, TCS_ENABLE(bank_slot(owner, i)), image[i].enable))
            return 0;
    owner->io.barrier(owner->io.ctx);
    return verify(owner, image, 0) && idle(owner);
}

static int program(rpmh_sleep_t* owner, const rpmh_sleep_bank_t* image)
{
    if (!memcmp(owner->current, image, sizeof(owner->current)))
        return 1;
    owner->modified = 1u;
    if (!write_image(owner, image)) {
        /* No firmware entry can start while PRE_EXECUTE is still running. Restore the
         * last verified image; retain the owner if any restoration write is uncertain. */
        if (!write_image(owner, owner->current))
            owner->faulted = 1u;
        return 0;
    }
    memcpy(owner->current, image, sizeof(owner->current));
    ++owner->writes;
    return 1;
}

int rpmh_sleep_enter(rpmh_sleep_t* owner)
{
    rpmh_sleep_result_t built;
    if (!authorized(owner) || !owner->bound)
        return result(owner, RPMH_SLEEP_OWNER);
    if (owner->faulted || owner->cache_invalid)
        return result(owner, RPMH_SLEEP_FAULT);
    if (owner->frozen || !idle(owner))
        return result(owner, RPMH_SLEEP_BUSY);
    if (!verify(owner, owner->current, 0))
        return result(owner, RPMH_SLEEP_CONFLICT);
    if (owner->revision != owner->staged_revision) {
        built = build(owner);
        if (built != RPMH_SLEEP_OK)
            return result(owner, built);
        if (!program(owner, owner->next))
            return result(owner, RPMH_SLEEP_IO);
        owner->staged_revision = owner->revision;
    }
    owner->frozen = 1u;
    ++owner->entries;
    return result(owner, RPMH_SLEEP_OK);
}

int rpmh_sleep_exit(rpmh_sleep_t* owner)
{
    if (!owner || !owner->frozen)
        return result(owner, RPMH_SLEEP_OWNER);
    if (!idle(owner) || !verify(owner, owner->current, 0)) {
        owner->faulted = 1u;
        return result(owner, RPMH_SLEEP_FAULT);
    }
    /* An OS completion ends the entry attempt; it does not prove DRIPS residency. */
    owner->frozen = 0u;
    ++owner->returns;
    return result(owner, RPMH_SLEEP_OK);
}

int rpmh_sleep_restore(rpmh_sleep_t* owner)
{
    if (!owner)
        return 0;
    if (owner->frozen || owner->faulted)
        return result(owner, RPMH_SLEEP_FAULT);
    if (!owner->bound)
        return 1;
    if (!idle(owner))
        return result(owner, RPMH_SLEEP_BUSY);
    if (!verify(owner, owner->current, 0))
        return result(owner, RPMH_SLEEP_CONFLICT);
    if (!program(owner, owner->baseline))
        return result(owner, RPMH_SLEEP_IO);
    owner->bound = owner->modified = 0u;
    return result(owner, RPMH_SLEEP_OK);
}
