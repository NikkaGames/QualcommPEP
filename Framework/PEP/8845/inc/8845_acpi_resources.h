#ifndef ACPI_RESOURCES_H
#define ACPI_RESOURCES_H
/* names establish resource identity, not numerical PPP ABI values or vote semantics. */

#ifdef __cplusplus
extern "C" {
#endif
typedef enum {
    PPP_LDO = 1,
    PPP_SMPS = 2,
    PPP_PMIC_CLOCK = 3,
    PPP_BUCK_BOOST = 4,
    PPP_OTHER = 5
} ppp_kind_t;

typedef struct {
    const char* name;
    const char* dts_regulator;
    ppp_kind_t kind;
    unsigned char dts_identity_unique;
    unsigned char original_8480_string;
} ppp_res_t;

extern const ppp_res_t ppp_catalog[];
extern const unsigned int ppp_name_cnt;
const ppp_res_t* lookup_ppp_name(const char* name);

typedef struct {
    const char* name;
    unsigned int unique_id;
    unsigned int gdscr_offset;
    unsigned int rail_mask;
} gdsc_res_t;

#define GDSC_RAIL_CX 1u
#define GDSC_RAIL_MX 2u
extern const gdsc_res_t gdscs[];
extern const unsigned int gdsc_cnt;
const gdsc_res_t* lookup_gdsc(const char* name);

typedef enum {
    ACPI_RES_UNKNOWN = 0,
    ACPI_RES_CLOCK = 1,
    ACPI_RES_GDSC = 2,
    ACPI_RES_PPP = 3,
    ACPI_RES_UNVERIFIED_ALIAS = 4
} acpi_res_type_t;

/* nparesource action: the NPA client res paths the original 8480 driver names. A desc's
 * path must be one of these; the level argument's units are NOT established, so the action
 * is validated and recorded, never executed. */

typedef struct {
    const char* path;
    unsigned char original_8480_string;
} npa_res_t;

extern const npa_res_t npa_resources[];
extern const unsigned int npa_resource_cnt;
const npa_res_t* lookup_npa_res(const char* path);
acpi_res_type_t classify_acpi_res(const char* name);
/* never alias gcc_disp_xo_clk to disp_cc_xo_clk implicitly. */
int is_unverified_clock_alias(const char* name);
#ifdef __cplusplus
}
#endif
#endif
