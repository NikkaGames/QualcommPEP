/* owner_attach_policy.h -- portable, fail-closed PEP0 owner slot admission for Windows
 * attach/restart. The caller samples the states before AND after detaching an old backend;
 * a failed detach can transition an otherwise clear slot to quarantined. */

#ifndef OWNER_ATTACH_POLICY_H
#define OWNER_ATTACH_POLICY_H

typedef enum owner_attach_decision {
    OWNER_ATTACH_ALLOW = 0,
    OWNER_ATTACH_QUARANTINED = 1,
    OWNER_ATTACH_RETAINED_GRAPH = 2,
    OWNER_ATTACH_FOREIGN_DEV = 3
} owner_attach_decision_t;

static __inline owner_attach_decision_t
owner_attach_decision(unsigned int quarantined_rsc, unsigned int retained_graph,
                      unsigned int existing_owner_from_other_dev)
{
    if (quarantined_rsc)
        return OWNER_ATTACH_QUARANTINED;
    if (retained_graph)
        return OWNER_ATTACH_RETAINED_GRAPH;
    if (existing_owner_from_other_dev)
        return OWNER_ATTACH_FOREIGN_DEV;
    return OWNER_ATTACH_ALLOW;
}
#endif
