/**
 * \file xf86drm.c
 * User-level interface to DRM device
 *
 * \author Rickard E. (Rik) Faith <faith@valinux.com>
 * \author Kevin E. Martin <martin@valinux.com>
 */

/*
 * Copyright 1999 Precision Insight, Inc., Cedar Park, Texas.
 * Copyright 2000 VA Linux Systems, Inc., Sunnyvale, California.
 * All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * PRECISION INSIGHT AND/OR ITS SUPPLIERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <unistd.h>
#include <string.h>
#include <strings.h>
#include <ctype.h>
#include <dirent.h>
#include <stddef.h>
#include <fcntl.h>
#include <errno.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#define stat_t struct stat
#include <sys/ioctl.h>
#include <sys/time.h>
#include <stdarg.h>
#ifdef MAJOR_IN_MKDEV
#include <sys/mkdev.h>
#endif
#ifdef MAJOR_IN_SYSMACROS
#include <sys/sysmacros.h>
#endif
#if HAVE_SYS_SYSCTL_H
#include <sys/sysctl.h>
#endif
#include <inttypes.h>

#if defined(__FreeBSD__)
#include <sys/param.h>
#include <sys/pciio.h>
#endif

#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))

/* Not all systems have MAP_FAILED defined */
#ifndef MAP_FAILED
#define MAP_FAILED ((void *)-1)
#endif

#include "xf86drm.h"
#include "libdrm_macros.h"
#include "drm_fourcc.h"

#define DRM_MODIFIER(v, f, f_name) \
       .modifier = DRM_FORMAT_MOD_##v ## _ ##f, \
       .modifier_name = #f_name

#define DRM_MODIFIER_INVALID(v, f_name) \
       .modifier = DRM_FORMAT_MOD_INVALID, .modifier_name = #f_name

#define DRM_MODIFIER_LINEAR(v, f_name) \
       .modifier = DRM_FORMAT_MOD_LINEAR, .modifier_name = #f_name

/* Intel is abit special as the format doesn't follow other vendors naming
 * scheme */
#define DRM_MODIFIER_INTEL(f, f_name) \
       .modifier = I915_FORMAT_MOD_##f, .modifier_name = #f_name

struct drmFormatModifierInfo {
    uint64_t modifier;
    const char *modifier_name;
};

struct drmFormatModifierVendorInfo {
    uint8_t vendor;
    const char *vendor_name;
};

#include "generated_static_table_fourcc.h"

struct drmVendorInfo {
    uint8_t vendor;
    char *(*vendor_cb)(uint64_t modifier);
};

struct drmFormatVendorModifierInfo {
    uint64_t modifier;
    const char *modifier_name;
};

static char *
drmGetFormatModifierNameFromArm(uint64_t modifier);

static char *
drmGetFormatModifierNameFromNvidia(uint64_t modifier);

static char *
drmGetFormatModifierNameFromAmd(uint64_t modifier);

static char *
drmGetFormatModifierNameFromAmlogic(uint64_t modifier);

static char *
drmGetFormatModifierNameFromVivante(uint64_t modifier);

static const struct drmVendorInfo modifier_format_vendor_table[] = {
    { DRM_FORMAT_MOD_VENDOR_ARM, drmGetFormatModifierNameFromArm },
    { DRM_FORMAT_MOD_VENDOR_NVIDIA, drmGetFormatModifierNameFromNvidia },
    { DRM_FORMAT_MOD_VENDOR_AMD, drmGetFormatModifierNameFromAmd },
    { DRM_FORMAT_MOD_VENDOR_AMLOGIC, drmGetFormatModifierNameFromAmlogic },
    { DRM_FORMAT_MOD_VENDOR_VIVANTE, drmGetFormatModifierNameFromVivante },
};

#ifndef AFBC_FORMAT_MOD_MODE_VALUE_MASK
#define AFBC_FORMAT_MOD_MODE_VALUE_MASK	0x000fffffffffffffULL
#endif

static const struct drmFormatVendorModifierInfo arm_mode_value_table[] = {
    { AFBC_FORMAT_MOD_YTR,          "YTR" },
    { AFBC_FORMAT_MOD_SPLIT,        "SPLIT" },
    { AFBC_FORMAT_MOD_SPARSE,       "SPARSE" },
    { AFBC_FORMAT_MOD_CBR,          "CBR" },
    { AFBC_FORMAT_MOD_TILED,        "TILED" },
    { AFBC_FORMAT_MOD_SC,           "SC" },
    { AFBC_FORMAT_MOD_DB,           "DB" },
    { AFBC_FORMAT_MOD_BCH,          "BCH" },
    { AFBC_FORMAT_MOD_USM,          "USM" },
};

static bool
drmGetAfbcFormatModifierNameFromArm(uint64_t modifier, FILE *fp)
{
    uint64_t mode_value = modifier & AFBC_FORMAT_MOD_MODE_VALUE_MASK;
    uint64_t block_size = mode_value & AFBC_FORMAT_MOD_BLOCK_SIZE_MASK;

    const char *block = NULL;
    const char *mode = NULL;
    bool did_print_mode = false;

    /* add block, can only have a (single) block */
    switch (block_size) {
    case AFBC_FORMAT_MOD_BLOCK_SIZE_16x16:
        block = "16x16";
        break;
    case AFBC_FORMAT_MOD_BLOCK_SIZE_32x8:
        block = "32x8";
        break;
    case AFBC_FORMAT_MOD_BLOCK_SIZE_64x4:
        block = "64x4";
        break;
    case AFBC_FORMAT_MOD_BLOCK_SIZE_32x8_64x4:
        block = "32x8_64x4";
        break;
    }

    if (!block) {
        return false;
    }

    fprintf(fp, "BLOCK_SIZE=%s,", block);

    /* add mode */
    for (unsigned int i = 0; i < ARRAY_SIZE(arm_mode_value_table); i++) {
        if (arm_mode_value_table[i].modifier & mode_value) {
            mode = arm_mode_value_table[i].modifier_name;
            if (!did_print_mode) {
                fprintf(fp, "MODE=%s", mode);
                did_print_mode = true;
            } else {
                fprintf(fp, "|%s", mode);
            }
        }
    }

    return true;
}

static bool
drmGetAfrcFormatModifierNameFromArm(uint64_t modifier, FILE *fp)
{
    bool scan_layout;
    for (unsigned int i = 0; i < 2; ++i) {
        uint64_t coding_unit_block =
          (modifier >> (i * 4)) & AFRC_FORMAT_MOD_CU_SIZE_MASK;
        const char *coding_unit_size = NULL;

        switch (coding_unit_block) {
        case AFRC_FORMAT_MOD_CU_SIZE_16:
            coding_unit_size = "CU_16";
            break;
        case AFRC_FORMAT_MOD_CU_SIZE_24:
            coding_unit_size = "CU_24";
            break;
        case AFRC_FORMAT_MOD_CU_SIZE_32:
            coding_unit_size = "CU_32";
            break;
        }

        if (!coding_unit_size) {
            if (i == 0) {
                return false;
            }
            break;
        }

        if (i == 0) {
            fprintf(fp, "P0=%s,", coding_unit_size);
        } else {
            fprintf(fp, "P12=%s,", coding_unit_size);
        }
    }

    scan_layout =
        (modifier & AFRC_FORMAT_MOD_LAYOUT_SCAN) == AFRC_FORMAT_MOD_LAYOUT_SCAN;
    if (scan_layout) {
        fprintf(fp, "SCAN");
    } else {
        fprintf(fp, "ROT");
    }
    return true;
}

static char *
drmGetFormatModifierNameFromArm(uint64_t modifier)
{
    uint64_t type = (modifier >> 52) & 0xf;

    FILE *fp;
    size_t size = 0;
    char *modifier_name = NULL;
    bool result = false;

    fp = open_memstream(&modifier_name, &size);
    if (!fp)
        return NULL;

    switch (type) {
    case DRM_FORMAT_MOD_ARM_TYPE_AFBC:
        result = drmGetAfbcFormatModifierNameFromArm(modifier, fp);
        break;
    case DRM_FORMAT_MOD_ARM_TYPE_AFRC:
        result = drmGetAfrcFormatModifierNameFromArm(modifier, fp);
        break;
    /* misc type is already handled by the static table */
    case DRM_FORMAT_MOD_ARM_TYPE_MISC:
    default:
        result = false;
        break;
    }

    fclose(fp);
    if (!result) {
        free(modifier_name);
        return NULL;
    }

    return modifier_name;
}

static char *
drmGetFormatModifierNameFromNvidia(uint64_t modifier)
{
    uint64_t height, kind, gen, sector, compression;

    height = modifier & 0xf;
    kind = (modifier >> 12) & 0xff;

    gen = (modifier >> 20) & 0x3;
    sector = (modifier >> 22) & 0x1;
    compression = (modifier >> 23) & 0x7;

    /* just in case there could other simpler modifiers, not yet added, avoid
     * testing against TEGRA_TILE */
    if ((modifier & 0x10) == 0x10) {
        char *mod_nvidia;
        if (asprintf(&mod_nvidia, "BLOCK_LINEAR_2D,HEIGHT=%"PRIu64",KIND=%"PRIu64","
                 "GEN=%"PRIu64",SECTOR=%"PRIu64",COMPRESSION=%"PRIu64"", height,
                 kind, gen, sector, compression) < 0)
            mod_nvidia = NULL;
        return mod_nvidia;
    }

    return  NULL;
}

static char *
drmGetFormatModifierNameFromAmd(uint64_t modifier)
{
    static const char *gfx9_gfx11_tile_strings[32] = {
        "LINEAR",
        "256B_S",
        "256B_D",
        "256B_R",
        "4KB_Z",
        "4KB_S",
        "4KB_D",
        "4KB_R",
        "64KB_Z",
        "64KB_S",
        "64KB_D",
        "64KB_R",
        "INVALID12",
        "INVALID13",
        "INVALID14",
        "INVALID15",
        "64KB_Z_T",
        "64KB_S_T",
        "64KB_D_T",
        "64KB_R_T",
        "4KB_Z_X",
        "4KB_S_X",
        "4KB_D_X",
        "4KB_R_X",
        "64KB_Z_X",
        "64KB_S_X",
        "64KB_D_X",
        "64KB_R_X",
        "256KB_Z_X",
        "256KB_S_X",
        "256KB_D_X",
        "256KB_R_X",
    };
    static const char *gfx12_tile_strings[32] = {
        "LINEAR",
        "256B_2D",
        "4KB_2D",
        "64KB_2D",
        "256KB_2D",
        "4KB_3D",
        "64KB_3D",
        "256KB_3D",
        /* other values are unused */
    };
    uint64_t tile_version = AMD_FMT_MOD_GET(TILE_VERSION, modifier);
    FILE *fp;
    char *mod_amd = NULL;
    size_t size = 0;

    fp = open_memstream(&mod_amd, &size);
    if (!fp)
        return NULL;

    switch (tile_version) {
    case AMD_FMT_MOD_TILE_VER_GFX9:
        fprintf(fp, "GFX9");
        break;
    case AMD_FMT_MOD_TILE_VER_GFX10:
        fprintf(fp, "GFX10");
        break;
    case AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS:
        fprintf(fp, "GFX10_RBPLUS");
        break;
    case AMD_FMT_MOD_TILE_VER_GFX11:
        fprintf(fp, "GFX11");
        break;
    case AMD_FMT_MOD_TILE_VER_GFX12:
        fprintf(fp, "GFX12");
        break;
    default:
        fclose(fp);
        free(mod_amd);
        return NULL;
    }

    if (tile_version >= AMD_FMT_MOD_TILE_VER_GFX12) {
        unsigned tile = AMD_FMT_MOD_GET(TILE, modifier);

        fprintf(fp, ",%s", gfx12_tile_strings[tile]);

        if (AMD_FMT_MOD_GET(DCC, modifier)) {
            fprintf(fp, ",DCC,DCC_MAX_COMPRESSED_BLOCK=%uB",
                    64 << AMD_FMT_MOD_GET(DCC_MAX_COMPRESSED_BLOCK, modifier));

            /* Other DCC fields are unused by GFX12. */
        }
    } else {
        unsigned tile = AMD_FMT_MOD_GET(TILE, modifier);

        fprintf(fp, ",%s", gfx9_gfx11_tile_strings[tile]);

        /* All *_T and *_X modes are affected by chip-specific fields. */
        if (tile >= 16) {
            fprintf(fp, ",PIPE_XOR_BITS=%u",
                    (unsigned)AMD_FMT_MOD_GET(PIPE_XOR_BITS, modifier));

            switch (tile_version) {
            case AMD_FMT_MOD_TILE_VER_GFX9:
                fprintf(fp, ",BANK_XOR_BITS=%u",
                        (unsigned)AMD_FMT_MOD_GET(BANK_XOR_BITS, modifier));
                break;

            case AMD_FMT_MOD_TILE_VER_GFX10:
                /* Nothing else for GFX10. */
                break;

            case AMD_FMT_MOD_TILE_VER_GFX10_RBPLUS:
            case AMD_FMT_MOD_TILE_VER_GFX11:
                /* This also determines the DCC layout, but DCC is only legal
                 * with tile=27 and tile=31 (*_R_X modes).
                 */
                fprintf(fp, ",PACKERS=%u",
                        (unsigned)AMD_FMT_MOD_GET(PACKERS, modifier));
                break;
            }
        }

        if (AMD_FMT_MOD_GET(DCC, modifier)) {
            if (tile_version == AMD_FMT_MOD_TILE_VER_GFX9 &&
                (AMD_FMT_MOD_GET(DCC_PIPE_ALIGN, modifier) ||
                 AMD_FMT_MOD_GET(DCC_RETILE, modifier))) {
                /* These two only determine the layout of
                 * the non-displayable DCC plane.
                 */
                fprintf(fp, ",RB=%u",
                        (unsigned)AMD_FMT_MOD_GET(RB, modifier));
                fprintf(fp, ",PIPE=%u",
                        (unsigned)AMD_FMT_MOD_GET(PIPE, modifier));
            }

            fprintf(fp, ",DCC,DCC_MAX_COMPRESSED_BLOCK=%uB",
                    64 << AMD_FMT_MOD_GET(DCC_MAX_COMPRESSED_BLOCK, modifier));

            if (AMD_FMT_MOD_GET(DCC_INDEPENDENT_64B, modifier))
                fprintf(fp, ",DCC_INDEPENDENT_64B");

            if (AMD_FMT_MOD_GET(DCC_INDEPENDENT_128B, modifier))
                fprintf(fp, ",DCC_INDEPENDENT_128B");

            if (AMD_FMT_MOD_GET(DCC_CONSTANT_ENCODE, modifier))
                fprintf(fp, ",DCC_CONSTANT_ENCODE");

            if (AMD_FMT_MOD_GET(DCC_PIPE_ALIGN, modifier))
                fprintf(fp, ",DCC_PIPE_ALIGN");

            if (AMD_FMT_MOD_GET(DCC_RETILE, modifier))
                fprintf(fp, ",DCC_RETILE");
        }
    }

    fclose(fp);
    return mod_amd;
}

static char *
drmGetFormatModifierNameFromAmlogic(uint64_t modifier)
{
    uint64_t layout = modifier & 0xff;
    uint64_t options = (modifier >> 8) & 0xff;
    char *mod_amlogic = NULL;

    const char *layout_str;
    const char *opts_str;

    switch (layout) {
    case AMLOGIC_FBC_LAYOUT_BASIC:
       layout_str = "BASIC";
       break;
    case AMLOGIC_FBC_LAYOUT_SCATTER:
       layout_str = "SCATTER";
       break;
    default:
       layout_str = "INVALID_LAYOUT";
       break;
    }

    if (options & AMLOGIC_FBC_OPTION_MEM_SAVING)
        opts_str = "MEM_SAVING";
    else
        opts_str = "0";

    if (asprintf(&mod_amlogic, "FBC,LAYOUT=%s,OPTIONS=%s", layout_str, opts_str) < 0)
        mod_amlogic = NULL;
    return mod_amlogic;
}

static char *
drmGetFormatModifierNameFromVivante(uint64_t modifier)
{
    const char *color_tiling, *tile_status, *compression;
    char *mod_vivante = NULL;

    switch (modifier & VIVANTE_MOD_TS_MASK) {
    case 0:
        tile_status = "";
        break;
    case VIVANTE_MOD_TS_64_4:
        tile_status = ",TS=64B_4";
        break;
    case VIVANTE_MOD_TS_64_2:
        tile_status = ",TS=64B_2";
        break;
    case VIVANTE_MOD_TS_128_4:
        tile_status = ",TS=128B_4";
        break;
    case VIVANTE_MOD_TS_256_4:
        tile_status = ",TS=256B_4";
        break;
    default:
        tile_status = ",TS=UNKNOWN";
        break;
    }

    switch (modifier & VIVANTE_MOD_COMP_MASK) {
    case 0:
        compression = "";
        break;
    case VIVANTE_MOD_COMP_DEC400:
        compression = ",COMP=DEC400";
        break;
    default:
        compression = ",COMP=UNKNOWN";
	break;
    }

    switch (modifier & ~VIVANTE_MOD_EXT_MASK) {
    case 0:
        color_tiling = "LINEAR";
	break;
    case DRM_FORMAT_MOD_VIVANTE_TILED:
        color_tiling = "TILED";
	break;
    case DRM_FORMAT_MOD_VIVANTE_SUPER_TILED:
        color_tiling = "SUPER_TILED";
	break;
    case DRM_FORMAT_MOD_VIVANTE_SPLIT_TILED:
        color_tiling = "SPLIT_TILED";
	break;
    case DRM_FORMAT_MOD_VIVANTE_SPLIT_SUPER_TILED:
        color_tiling = "SPLIT_SUPER_TILED";
	break;
    default:
        color_tiling = "UNKNOWN";
	break;
    }

    if (asprintf(&mod_vivante, "%s%s%s", color_tiling, tile_status, compression) < 0)
        mod_vivante = NULL;
    return mod_vivante;
}


static char *
drmGetFormatModifierFromSimpleTokens(uint64_t modifier)
{
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(drm_format_modifier_table); i++) {
        if (drm_format_modifier_table[i].modifier == modifier)
            return strdup(drm_format_modifier_table[i].modifier_name);
    }

    return NULL;
}

/** Retrieves a human-readable representation of a vendor (as a string) from
 * the format token modifier
 *
 * \param modifier the format modifier token
 * \return a char pointer to the human-readable form of the vendor. Caller is
 * responsible for freeing it.
 */
drm_public char *
drmGetFormatModifierVendor(uint64_t modifier)
{
    unsigned int i;
    uint8_t vendor = fourcc_mod_get_vendor(modifier);

    for (i = 0; i < ARRAY_SIZE(drm_format_modifier_vendor_table); i++) {
        if (drm_format_modifier_vendor_table[i].vendor == vendor)
            return strdup(drm_format_modifier_vendor_table[i].vendor_name);
    }

    return NULL;
}

/** Retrieves a human-readable representation string from a format token
 * modifier
 *
 * If the dedicated function was not able to extract a valid name or searching
 * the format modifier was not in the table, this function would return NULL.
 *
 * \param modifier the token format
 * \return a malloc'ed string representation of the modifier. Caller is
 * responsible for freeing the string returned.
 *
 */
drm_public char *
drmGetFormatModifierName(uint64_t modifier)
{
    uint8_t vendorid = fourcc_mod_get_vendor(modifier);
    char *modifier_found = NULL;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(modifier_format_vendor_table); i++) {
        if (modifier_format_vendor_table[i].vendor == vendorid)
            modifier_found = modifier_format_vendor_table[i].vendor_cb(modifier);
    }

    if (!modifier_found)
        return drmGetFormatModifierFromSimpleTokens(modifier);

    return modifier_found;
}

/**
 * Get a human-readable name for a DRM FourCC format.
 *
 * \param format The format.
 * \return A malloc'ed string containing the format name. Caller is responsible
 * for freeing it.
 */
drm_public char *
drmGetFormatName(uint32_t format)
{
    char *str, code[5];
    const char *be;
    size_t str_size, i;

    be = (format & DRM_FORMAT_BIG_ENDIAN) ? "_BE" : "";
    format &= ~DRM_FORMAT_BIG_ENDIAN;

    if (format == DRM_FORMAT_INVALID)
        return strdup("INVALID");

    code[0] = (char) ((format >> 0) & 0xFF);
    code[1] = (char) ((format >> 8) & 0xFF);
    code[2] = (char) ((format >> 16) & 0xFF);
    code[3] = (char) ((format >> 24) & 0xFF);
    code[4] = '\0';

    /* Trim spaces at the end */
    for (i = 3; i > 0 && code[i] == ' '; i--)
        code[i] = '\0';

    str_size = strlen(code) + strlen(be) + 1;
    str = malloc(str_size);
    if (!str)
        return NULL;

    snprintf(str, str_size, "%s%s", code, be);

    return str;
}
