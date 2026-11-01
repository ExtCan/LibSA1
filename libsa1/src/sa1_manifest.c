/*
 * Address manifest for Sonic Adventure (Dreamcast) US v1.005 (MK-51000).
 *
 * This file holds METADATA only: file names, sizes and SHA-1 hashes used to
 * recognise the user's disc, overlay load addresses, and the SH-4 addresses of the
 * tables the asset loader starts from.  No game content is embedded.  Addresses
 * come from sadc/docs/research/sa1_assets.md (measured on the disc and cross-checked
 * with the decompilation); another build of the game needs its own manifest.
 */
#include "sa1_internal.h"

static const sa1_file_desc k_files[] = {
    /* executable */
    { "1ST_READ.BIN",   6735488, SA1_FIRST_READ_SHA1,                         0x8C010000u },
    /* motion overlays: PRS-compressed memory images with absolute pointers */
    { "S_MOT.PRS",       214890, "4b16f21911a3aa999104e74ff38c2ef032d160a2", 0x0CC00000u },
    { "K_MOT.PRS",       158391, "b6d4f876ebf78e56fc289d8368926415de16c26a", 0x0CC00000u },
    { "A_MOT.PRS",       258637, "c407214fa005c477f352ac61feb8ad47433d6612", 0x0CC00000u },
    { "B_MOT.PRS",       269418, "30a201b0c20ab7b7d1d1e55efc9f61757bedcd1c", 0x0CC00000u },
    { "E_MOT.PRS",        89206, "a50fcfe3b24613b4b77a3b27d26e9247beab3085", 0x0CC00000u },
    { "S_SBMOT.PRS",     117125, "39535c7e029e86b50634c9767c5a49c6e00d129e", 0x0CB08000u },
    { "M_SBMOT.PRS",      74957, "e93e325a592ad206ac8df7e5f5b107fbd2d98e8f", 0x0CADC000u },
    { "TIKAL_PROG.PRS",  105985, "d5245a7e12ce6cff02f415b72f175fe4999747bb", 0x0CB00000u },
    { "B_CHAOS7.PRS",    873457, "e43aa8955683a173b12f43fb03c3e9b25589f5d0", 0x0C900000u },
    { "MINICART.PRS",    331317, "80196ba46b650b9e11bcbbdd570b47a98f9a51f4", 0x0C900000u },
    { "STG02.PRS",       733811, "7f8e243a8b581ab3a522f2e73c0d2344aa41d1f7", 0x0C900000u },
    { "STG04.PRS",       765273, "dbdfe4f2ed9add9bd27b51bf0bd81e5a665c251f", 0x0C900000u },
    { "STG06.PRS",       799177, "a0a7b51c62c4debfbc395db37505d4c5394d749d", 0x0C900000u },
    { "STG07.PRS",       660228, "53da7bc4091acfbeb993f2d16b56c72f5d10c2fa", 0x0C900000u },
    { "STG08.PRS",      1423684, "9477b39e5f52cf820d8f55d661267a08fc30d585", 0x0C900000u },
    { "STG09.PRS",      1180070, "581f4a4b6cd7f263300f7d0a0b75cd56fe4aede1", 0x0C900000u },
    /* texture archives (PRS-compressed PVM) */
    { "SONIC.PRS",        36861, "3102d9f6c9735a316995b783f671894f96e038e7", 0 },
    { "SON_EFF.PRS",       5676, "ee23c235f45489fb2b7ced21716b9f22ede2e550", 0 },
    { "MILES.PRS",        37906, "1a524c170be1998c89a81bbc9c108f938734662a", 0 },
    { "KNUCKLES.PRS",     27423, "1781bd2c0b6211c12db314183e46bece8e59bbe5", 0 },
    { "KNU_EFF.PRS",      22327, "f5a8453198105d562501e22b5031dfc45929acaf", 0 },
    { "AMY.PRS",          67183, "c1018ce11cf1f1ab1e5e23a3a7afe5d70440432f", 0 },
    { "AMY_EFF.PRS",       1823, "e6173468e51e6d9c9f8f3dd5649219b717634b40", 0 },
    { "BIG.PRS",          44820, "1e8c6493ef5be6f960c45e04b42b908bbb8fbd80", 0 },
    { "FISHING.PRS",     117120, "a69c3d61f886d556874cbf4e62ec1f90c6b08959", 0 },
    { "BIG_KAERU.PRS",    33836, "18243a22513058c42cf71c2932526f4c95ca7312", 0 },
    { "E102.PRS",        105038, "39fe1fdc874a8df09d6746a215b60f667bf617d0", 0 },
    { "E102BEAM.PRS",     21296, "dcba184fbaac1ca36f1d0281c65d5e02f180afd0", 0 },
    { "E102EFFECT.PRS",   13134, "35151104f9267fa120ac9ec48e8faec6572f2613", 0 },
    { "E102TIME.PRS",      6892, "5dd2c835a18b07a03fedbd167ab30f2cfa10130e", 0 },
    { "E102HIT.PRS",      12586, "c841227db9a5dbfe7e8c07cc4c0e885858e8847e", 0 },
    { "EGGMAN.PRS",       35507, "35adc715a1eec0e9a610666d7574fc221009cdcd", 0 },
    { "TIKAL.PRS",        38750, "2b278f7e25b625e6530b19e0f1899e337f8c4627", 0 },
    { "SUPERSONIC.PRS",   79512, "b81d59bba5d083855a9be94521032b83378f1936", 0 },
    { "OBJ_REGULAR.PRS", 155785, "446e5450ab93d62cf6578dab419e095d33ca2c6a", 0 },
    { "EFF_REGULAR.PRS", 124234, "e6c36621a4624c076ddb053320e3f5c989ec4899", 0 },
    { "CON_REGULAR.PRS",  27268, "52639ac35660cd45926cd45756427fbc20127992", 0 },
    /* round 0.9.5 (the board, SA1_LOAD_BOARDS; sadc docs/research/board095_lib.md 2.6): standalone PVRs, uncompressed
       (GBIX + PVRT), one 64 x 64 RGB565 twiddled + mips texture each */
    { "MIW_B001.PVR",     10952, "7eb21e78fa9d54ef00029043be91285bdde1de32", 0 },
    { "SB_BOARD1.PVR",    10956, "d851ade3e5d8b610790a25aca1e8960aeae9bde6", 0 },
    { "MTX_BOARD0.PVR",   10956, "f41d0bf31bc1af46cf683bc55343161d519fee7a", 0 },
};

const sa1_file_desc *sa1_manifest_file(const char *name)
{
    size_t i;
    for (i = 0; i < sizeof k_files / sizeof k_files[0]; i++)
        if (strcmp(k_files[i].name, name) == 0)
            return &k_files[i];
    return NULL;
}

/* Actions whose NJS_ACTION lives in an overlay that shares its load address with
 * other overlays (0x0C900000: STGxx, MINICART, B_CHAOS7).  These must be resolved in
 * exactly that overlay; if it is not loaded the action is "not loaded" (the table
 * still holds the stale overlay pointer). */
static const sa1_action_override k_sonic_over[] = {
    { 40, 40, "STG02.PRS", SA1_LOAD_STAGE_ACTIONS },
    { 42, 42, "STG02.PRS", SA1_LOAD_STAGE_ACTIONS },
    { 44, 45, "STG02.PRS", SA1_LOAD_STAGE_ACTIONS },
    { 46, 46, "STG04.PRS", SA1_LOAD_STAGE_ACTIONS },
    { 88, 88, "STG07.PRS", SA1_LOAD_STAGE_ACTIONS },
    { 89, 93, "STG06.PRS", SA1_LOAD_STAGE_ACTIONS },
    { 94, 101, "STG08.PRS", SA1_LOAD_STAGE_ACTIONS },
    { 126, 129, "STG09.PRS", SA1_LOAD_STAGE_ACTIONS },
    { 130, 141, "B_CHAOS7.PRS", SA1_LOAD_SUPER },
};
static const sa1_action_override k_tails_over[] = { { 89, 89, "MINICART.PRS", SA1_LOAD_MINICART } };
static const sa1_action_override k_knuckles_over[] = { { 36, 36, "MINICART.PRS", SA1_LOAD_MINICART } };
static const sa1_action_override k_amy_over[] = { { 76, 76, "MINICART.PRS", SA1_LOAD_MINICART } };
static const sa1_action_override k_big_over[] = { { 96, 96, "MINICART.PRS", SA1_LOAD_MINICART } };
static const sa1_action_override k_gamma_over[] = { { 72, 72, "MINICART.PRS", SA1_LOAD_MINICART } };

/* Super Sonic runs Sonic's action table on his own models (provisional mapping:
 * Sonic main model -> Super Sonic main model, jump ball -> Super Sonic jump ball). */
static const uint32_t k_super_subst[][2] = {
    { 0x8C1CE76Cu, 0x0C909D2Cu },
    { 0x8C1DBB2Cu, 0x0C90BD04u },
};

#define N(a) (uint32_t)(sizeof(a) / sizeof((a)[0]))

static const sa1_char_desc k_chars[SA1_CHAR_COUNT] = {
    /* SA1_CHAR_SONIC */
    { "sonic", 0, 0x8C1CE76Cu, 0x8C1C55A0u, 142, 0x8C1C5FA4u,
      { "S_MOT.PRS", "S_SBMOT.PRS", "B_CHAOS7.PRS", NULL }, { 0, SA1_LOAD_BOARDS, SA1_LOAD_SUPER, 0 },
      k_sonic_over, N(k_sonic_over), NULL, 0, NULL, 0 },
    /* SA1_CHAR_EGGMAN */
    { "eggman", 1, 0x8C57A6BCu, 0x8C57008Cu, 24, 0x8C5702BCu,
      { NULL, NULL, NULL, NULL }, { 0, 0, 0, 0 }, NULL, 0, NULL, 0, NULL, 0 },
    /* SA1_CHAR_TAILS (the game also loads S_MOT.PRS for Tails, but no Tails action uses it) */
    { "tails", 2, 0x8C25F968u, 0x8C256D6Cu, 132, 0x8C2576ECu,
      { "M_SBMOT.PRS", NULL, NULL, NULL }, { SA1_LOAD_BOARDS, 0, 0, 0 },
      k_tails_over, N(k_tails_over), NULL, 0, NULL, 0 },
    /* SA1_CHAR_KNUCKLES */
    { "knuckles", 3, 0x8C35B094u, 0x8C3528CCu, 111, 0x8C353114u,
      { "K_MOT.PRS", NULL, NULL, NULL }, { 0, 0, 0, 0 },
      k_knuckles_over, N(k_knuckles_over), NULL, 0, NULL, 0 },
    /* SA1_CHAR_TIKAL (no action table; the model lives in TIKAL_PROG) */
    { "tikal", 4, 0x0CB09818u, 0, 0, 0x0CB015A8u,
      { "TIKAL_PROG.PRS", NULL, NULL, NULL }, { 0, 0, 0, 0 }, NULL, 0, NULL, 0, NULL, 0 },
    /* SA1_CHAR_AMY */
    { "amy", 5, 0x8C4C7C3Cu, 0x8C4BB1D0u, 98, 0x8C4BB8B4u,
      { "A_MOT.PRS", NULL, NULL, NULL }, { 0, 0, 0, 0 },
      k_amy_over, N(k_amy_over), NULL, 0, NULL, 0 },
    /* SA1_CHAR_GAMMA */
    { "gamma", 6, 0x8C42CC34u, 0x8C4225D4u, 87, 0x8C422BE8u,
      { "E_MOT.PRS", NULL, NULL, NULL }, { 0, 0, 0, 0 },
      k_gamma_over, N(k_gamma_over), NULL, 0, NULL, 0 },
    /* SA1_CHAR_BIG */
    { "big", 7, 0x8C52F298u, 0x8C525564u, 101, 0x8C525C90u,
      { "B_MOT.PRS", NULL, NULL, NULL }, { 0, 0, 0, 0 },
      k_big_over, N(k_big_over), NULL, 0, NULL, 0 },
    /* SA1_CHAR_SUPER_SONIC (B_CHAOS7 boss overlay; physics entry 0; Sonic's weld table
     * holds 12 entries for the Super Sonic objects) */
    { "supersonic", 0, 0x0C909D2Cu, 0x8C1C55A0u, 142, 0x8C1C5FA4u,
      { "B_CHAOS7.PRS", "S_MOT.PRS", NULL, NULL }, { 0, 0, 0, 0 },
      k_sonic_over, N(k_sonic_over), k_super_subst, N(k_super_subst), "SUPERSONIC", 0x0C901EA0u },
};

const sa1_char_desc *sa1_manifest_char(int32_t kind)
{
    if (kind < 0 || kind >= SA1_CHAR_COUNT)
        return NULL;
    return &k_chars[kind];
}

/* ------------------------------------------------------------------------- */
/* Address views                                                             */
/* ------------------------------------------------------------------------- */

const uint8_t *sa1_view_get(const sa1_view *v, uint32_t addr, uint32_t len, const sa1_image **which)
{
    uint32_t phys, i;
    if (!v || addr == 0)
        return NULL;
    phys = addr & 0x1FFFFFFFu;
    for (i = 0; i < v->n; i++) {
        const sa1_image *im = v->img[i];
        uint32_t off;
        if (!im || !im->data || phys < im->phys)
            continue;
        off = phys - im->phys;
        if (off > im->size || len > im->size - off)
            continue;
        if (which)
            *which = im;
        return im->data + off;
    }
    return NULL;
}

int sa1_images_overlap(const sa1_image *a, const sa1_image *b)
{
    uint64_t a0 = a->phys, a1 = (uint64_t)a->phys + a->size;
    uint64_t b0 = b->phys, b1 = (uint64_t)b->phys + b->size;
    return a0 < b1 && b0 < a1;
}