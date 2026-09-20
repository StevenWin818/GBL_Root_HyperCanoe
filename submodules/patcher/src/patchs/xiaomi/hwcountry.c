/* Xiaomi HwCountry cross-region patch.
 * Ported from GBL_Root_HyperCanoe legacy branch (tools/patchlib.h).
 *
 * Strategy: place "GLOBAL" and the constant line "HwCountry: GLOBAL"
 * into a zero-padded slack area, then retarget the ADRP+ADD pair that
 * feeds the HwCountry data pointer (fastboot getvar path) and the pair
 * that references the "HwCountry: %a" format string (display path).
 * All steps are idempotent: on an already-patched image the retargeted
 * display pair (now pointing at "HwCountry: GLOBAL") serves as the anchor
 * for auto-detection, and each patch site is recognized and kept.
 */
#include "patchs/xiaomi/hwcountry.h"
#include "arm64_inst/utils.h"

static int64_t find_cstring(const char* buffer, int32_t size, const char* needle) {
    int32_t len = (int32_t)strlen(needle) + 1;
    if (len <= 0 || size < len) return -1;
    for (int32_t i = 0; i <= size - len; ++i) {
        if (memcmp(buffer + i, needle, (size_t)len) == 0) return i;
    }
    return -1;
}

static int32_t find_zero_run(const char* buffer, int32_t size, int32_t start, int32_t end, int32_t need) {
    if (need <= 0) return -1;
    if (start < 0) start = 0;
    if (end > size) end = size;
    if (start >= end) return -1;

    int32_t run_start = -1;
    int32_t run_len = 0;

    for (int32_t i = start; i < end; ++i) {
        if ((uint8_t)buffer[i] == 0) {
            if (run_start < 0) run_start = i;
            run_len++;
            if (run_len >= need) return run_start;
        } else {
            run_start = -1;
            run_len = 0;
        }
    }
    return -1;
}

static int64_t ensure_cstring(char* buffer, int32_t size, int32_t* cursor, int32_t end, const char* needle) {
    int64_t existing = find_cstring(buffer, size, needle);
    if (existing >= 0) return existing;

    int32_t len = (int32_t)strlen(needle) + 1;
    if (*cursor < 0 || *cursor + len > end || *cursor + len > size) return -1;

    memcpy(buffer + *cursor, needle, (size_t)len);
    existing = *cursor;
    *cursor += len;
    return existing;
}

static uint32_t encode_adrp_target(uint8_t rd, int32_t instr_off, uint64_t load_base, uint64_t target_off) {
    int64_t pc_page = ((int64_t)load_base + instr_off) & ~0xFFFLL;
    int64_t target_page = ((int64_t)load_base + (int64_t)target_off) & ~0xFFFLL;
    int64_t page_delta = (target_page - pc_page) >> 12;
    uint32_t imm21 = (uint32_t)((uint64_t)page_delta & 0x1FFFFFull);
    uint32_t immlo = imm21 & 0x3u;
    uint32_t immhi = (imm21 >> 2) & 0x7FFFFu;

    return 0x90000000u | (immlo << 29) | (immhi << 5) | (rd & 0x1Fu);
}

static uint32_t encode_add_x_imm_target(uint8_t rd, uint8_t rn, uint32_t imm) {
    uint32_t imm12 = 0;
    uint32_t shift = 0;

    if (imm < 0x1000u) {
        imm12 = imm;
    } else if ((imm & 0xFFFu) == 0 && (imm >> 12) < 0x1000u) {
        imm12 = imm >> 12;
        shift = 1;
    } else {
        return 0;
    }

    return 0x91000000u
         | (shift << 22)
         | ((imm12 & 0xFFFu) << 10)
         | ((uint32_t)(rn & 0x1Fu) << 5)
         | (rd & 0x1Fu);
}

static int32_t patch_fixed_adrl_target(char* buffer, int32_t size, int32_t adrp_off, uint64_t load_base,
                                       int64_t expected_target, int64_t new_target, const char* tag) {
    if (adrp_off < 0 || adrp_off + 8 > size) {
        printf("%s patch offset out of range\n", tag);
        return -1;
    }

    DecodedInst d0 = decode_at(buffer, adrp_off);
    DecodedInst d1 = decode_at(buffer, adrp_off + 4);
    if (d0.type != INST_ADRP || d1.type != INST_ADD_X_IMM || d1.rn != d0.rt) {
        printf("%s patch site is not ADRP+ADD\n", tag);
        return -1;
    }

    int64_t current_target = calc_adrl_file_offset(buffer, adrp_off, load_base);
    if (current_target == new_target) {
        printf("%s already patched -> file:0x%llX\n",
               tag, (unsigned long long)new_target);
        return 0;
    }

    if (current_target != expected_target) {
        printf("%s expected file:0x%llX, found file:0x%llX\n",
               tag,
               (unsigned long long)expected_target,
               (unsigned long long)current_target);
        return -1;
    }

    uint32_t new_adrp = encode_adrp_target(d0.rt, adrp_off, load_base, (uint64_t)new_target);
    uint32_t new_add  = encode_add_x_imm_target(d1.rt, d1.rn, (uint32_t)(new_target & 0xFFF));
    if (new_add == 0) {
        printf("%s new ADD immediate is out of range\n", tag);
        return -1;
    }

    printf("%s patch: file:0x%llX -> file:0x%llX\n",
           tag,
           (unsigned long long)current_target,
           (unsigned long long)new_target);

    write_instr(buffer, adrp_off, new_adrp);
    write_instr(buffer, adrp_off + 4, new_add);
    return 0;
}

static int32_t patch_first_adrl_by_string(char* buffer, int32_t size, uint64_t load_base,
                                          const char* old_str, int64_t new_target,
                                          const char* tag) {
    for (int32_t i = 0; i <= size - 8; i += 4) {
        DecodedInst d0 = decode_at(buffer, i);
        DecodedInst d1 = decode_at(buffer, i + 4);

        if (d0.type != INST_ADRP || d1.type != INST_ADD_X_IMM) continue;
        if (d1.rn != d0.rt) continue;

        int64_t current_target = calc_adrl_file_offset(buffer, i, load_base);
        if (current_target == new_target) {
            printf("%s already patched -> file:0x%llX\n",
                   tag, (unsigned long long)new_target);
            return 0;
        }

        if (!str_at(buffer, size, current_target, old_str)) continue;

        uint32_t new_adrp = encode_adrp_target(d0.rt, i, load_base, (uint64_t)new_target);
        uint32_t new_add  = encode_add_x_imm_target(d1.rt, d1.rn, (uint32_t)(new_target & 0xFFF));
        if (new_add == 0) {
            printf("%s new ADD immediate is out of range\n", tag);
            return -1;
        }

        printf("%s patch: file:0x%llX \"%s\" -> file:0x%llX\n",
               tag,
               (unsigned long long)current_target,
               old_str,
               (unsigned long long)new_target);

        write_instr(buffer, i, new_adrp);
        write_instr(buffer, i + 4, new_add);
        return 0;
    }

    printf("%s source string \"%s\" not found\n", tag, old_str);
    return -1;
}

/*
 * 自动定位 HwCountry 数据地址及其 getvar 引用的 ADRP+ADD 位置。
 * 锚点兼容已打补丁状态：display 补丁后不再有 ADRP 引用原格式串，
 * 但改指后的常量行 "HwCountry: GLOBAL" 位于同一指令链上，可作替代锚点；
 * 链上其余指令（ADD X3、base 赋值）不被补丁改写。
 * patched_value_off >= 0 时，终局查找也接受已改指该地址的 getvar 对。
 * 成功返回 0，失败返回 -1。
 */
static int32_t auto_find_hwcountry_ptrs(char* buffer, int32_t size, uint64_t load_base,
                                        int64_t patched_value_off,
                                        int32_t* out_adrp_off, int64_t* out_data_off) {
    if (out_adrp_off == 0 || out_data_off == 0) return -1;
    *out_adrp_off = -1;
    *out_data_off = -1;

    int64_t str_off = find_cstring(buffer, size, "HwCountry: %a");
    int64_t line_off = find_cstring(buffer, size, "HwCountry: GLOBAL");
    if (str_off < 0 && line_off < 0) {
        printf("Auto-detect: HwCountry strings not found\n");
        return -1;
    }

    int32_t fmt_adrp_off = -1;
    for (int32_t i = 0; i <= size - 8; i += 4) {
        int64_t t = calc_adrl_file_offset(buffer, i, load_base);
        if (t == str_off || t == line_off) {
            fmt_adrp_off = i;
            break;
        }
    }
    if (fmt_adrp_off < 0) {
        printf("Auto-detect: reference to \"HwCountry: %%a\" not found\n");
        return -1;
    }

    uint8_t base_reg = 0xFF;
    uint32_t add_imm = 0;
    int32_t scan_end = fmt_adrp_off + 32;
    if (scan_end > size - 4) scan_end = size - 4;

    for (int32_t i = fmt_adrp_off; i <= scan_end; i += 4) {
        DecodedInst d = decode_at(buffer, i);
        if (d.type == INST_ADD_X_IMM && d.rt == 3) {
            base_reg = d.rn;
            add_imm = d.imm;
            break;
        }
    }
    if (base_reg == 0xFF) {
        printf("Auto-detect: ADD to X3 not found near format usage\n");
        return -1;
    }

    int64_t base_val = -1;
    for (int32_t i = fmt_adrp_off; i >= 0; i -= 4) {
        DecodedInst d0 = decode_at(buffer, i);
        DecodedInst d1 = decode_at(buffer, i + 4);
        if (d0.type == INST_ADRP && d1.type == INST_ADD_X_IMM
            && d0.rt == base_reg && d1.rt == base_reg && d1.rn == base_reg) {
            base_val = calc_adrl_file_offset(buffer, i, load_base);
            break;
        }
    }
    if (base_val < 0) {
        printf("Auto-detect: base register assignment not found\n");
        return -1;
    }

    int64_t data_off = base_val + (int64_t)add_imm;
    *out_data_off = data_off;

    for (int32_t i = 0; i <= size - 8; i += 4) {
        if (calc_adrl_file_offset(buffer, i, load_base) == data_off) {
            *out_adrp_off = i;
            printf("Auto-detect: getvar ADRP found at 0x%X, data file off 0x%llX\n",
                   i, (unsigned long long)data_off);
            return 0;
        }
    }

    /* getvar 对已改指 slack 中的 "GLOBAL" 时走不到上面的分支。 */
    if (patched_value_off >= 0) {
        for (int32_t i = 0; i <= size - 8; i += 4) {
            if (calc_adrl_file_offset(buffer, i, load_base) == patched_value_off) {
                *out_adrp_off = i;
                *out_data_off = data_off;
                printf("Auto-detect: getvar ADRP found at 0x%X (already patched), original data off 0x%llX\n",
                       i, (unsigned long long)data_off);
                return 0;
            }
        }
    }

    printf("Auto-detect: final ADRP reference for data off 0x%llX not found\n",
           (unsigned long long)data_off);
    return -1;
}

bool patch_xiaomi_hwcountry(char* buffer, int32_t size, uint64_t load_base) {
    const char hwcountry_value[] = "GLOBAL";
    const char hwcountry_line[]  = "HwCountry: GLOBAL";
    const char hwcountry_fmt[]   = "HwCountry: %a";

    /* Gate: neither the format string nor an already-patched constant line
     * means this is not a Xiaomi ABL carrying the HwCountry code path;
     * skip rather than failing the whole patch. */
    if (find_cstring(buffer, size, hwcountry_fmt) < 0
        && find_cstring(buffer, size, hwcountry_line) < 0) {
        printf("Xiaomi HwCountry: format string \"HwCountry: %%a\" not found, skipping\n");
        return true;
    }

    /* Slack window for the replacement strings. The legacy patch hardcoded
     * 0x7F000-0x80000 (large CN/EEA images); smaller images run out of
     * bounds there, so fall back to scanning the second half of the file. */
    int32_t need = (int32_t)(strlen(hwcountry_value) + strlen(hwcountry_line) + 2);
    int32_t slack_start = 0x7F000;
    int32_t slack_end   = 0x80000;
    int32_t cursor = find_zero_run(buffer, size, slack_start, slack_end, need);
    if (cursor < 0) {
        slack_start = size / 2;
        slack_end   = size;
        cursor = find_zero_run(buffer, size, slack_start, slack_end, need);
        if (cursor < 0) {
            printf("HwCountry patch: no slack space found\n");
            return false;
        }
        printf("HwCountry patch: preferred slack 0x7F000-0x80000 unavailable, using 0x%X-0x%X\n",
               slack_start, slack_end);
    }
    if (cursor > 0 && buffer[cursor - 1] != 0) cursor++;

    int64_t value_off = ensure_cstring(buffer, size, &cursor, slack_end, hwcountry_value);
    int64_t line_off  = ensure_cstring(buffer, size, &cursor, slack_end, hwcountry_line);
    if (value_off < 0 || line_off < 0) {
        printf("HwCountry patch: failed to place replacement strings\n");
        return false;
    }

    int32_t target_adrp_off = -1;
    int64_t target_data_off = -1;
    if (auto_find_hwcountry_ptrs(buffer, size, load_base, value_off,
                                 &target_adrp_off, &target_data_off) != 0) {
        printf("HwCountry patch: failed to auto-locate getvar pointer\n");
        return false;
    }

    if (patch_fixed_adrl_target(buffer, size, target_adrp_off, load_base,
                                target_data_off, value_off, "HwCountry getvar") != 0) {
        return false;
    }

    if (patch_first_adrl_by_string(buffer, size, load_base, hwcountry_fmt, line_off,
                                   "HwCountry display") != 0) {
        return false;
    }

    return true;
}
