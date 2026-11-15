#ifndef SMALI_ANNDIR_H
#define SMALI_ANNDIR_H

#include "smali_annsec.h"

/* Every annotation of a DEX, from collection to the annotations_directory
 * items. Each class owns a run of slots holding set ids:
 *   [class] [static fields] [instance fields] [direct methods] [virtual methods]
 * and each method with annotated parameters owns a run of `params` slots,
 * one per parameter of its proto. */
typedef struct {
    smali_annsec_t sec;
    uint32_t *base;       /* per class: first slot */
    uint32_t *slots;      /* set id, or SMALI_NO_SET */
    uint32_t *pstart;     /* per method slot: first params slot, or SMALI_NO_SET */
    uint32_t *refoff;     /* per method slot: annotation_set_ref_list offset */
    uint32_t *params;
    uint32_t *dir_off;    /* per class: annotations_directory_item offset */
    uint32_t nslots, nparams;
    uint32_t ref_first, ref_count, dir_first, dir_count;
} smali_anndir_t;

/* Interns every annotation and set of the classes. Returns -1 on OOM. */
int smali_anndir_collect(smali_anndir_t *d, smali_ctx_def_t *ctx);

/* Writes annotation_set_ref_lists then annotations_directory_items, walking
 * the classes in `order` (class indices sorted by type, as smali.jar does). */
void smali_anndir_write(smali_anndir_t *d, smali_ctx_def_t *ctx, smali_buf_t *b,
                        const uint32_t *order);

void smali_anndir_free(smali_anndir_t *d);

#endif
