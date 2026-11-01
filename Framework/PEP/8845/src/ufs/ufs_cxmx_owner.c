#include "clock_target.h"
#include "ufs_cxmx_owner.h"

static const domain_rail_spec_t rails = {clock_ufs_domain,
                                         UFS_RAIL_CLIENT,
                                         {UFS_CX_NAME, UFS_MX_NAME},
                                         {UFS_CX_LEVEL, UFS_MX_LEVEL},
                                         RAIL_VDD_CX | RAIL_VDD_MX};

int ufs_cx_mx_bind(ufs_cxmx_owner_t* o, const ufs_cxmx_io_t* io, uint64_t epoch)
{
    return domain_rails_bind(o, &rails, io, epoch);
}

int ufs_cx_mx_rail_provider(ufs_cxmx_owner_t* o, fs_gdsc_rail_provider_t* provider)
{
    return domain_rails_provider(o, provider);
}

int ufs_cx_mx_retire(ufs_cxmx_owner_t* o)
{
    return domain_rails_retire(o);
}
