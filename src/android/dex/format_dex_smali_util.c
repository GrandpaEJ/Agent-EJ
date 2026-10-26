#include "format_dex_internal.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

const char *smali_aflags(uint32_t f) {
    static char buf[128]; int pos = 0;
    buf[0] = 0;
    if (f & 0x0001) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"public"); }
    if (f & 0x0002) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"private"); }
    if (f & 0x0004) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"protected"); }
    if (f & 0x0008) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"static"); }
    if (f & 0x0010) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"final"); }
    if (f & 0x0020) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"synchronized"); }
    if (f & 0x0040) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"bridge"); }
    if (f & 0x0080) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"varargs"); }
    if (f & 0x0100) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"native"); }
    if (f & 0x0200) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"interface"); }
    if (f & 0x0400) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"abstract"); }
    if (f & 0x0800) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"strictfp"); }
    if (f & 0x1000) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"synthetic"); }
    if (f & 0x2000) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"annotation"); }
    if (f & 0x4000) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"enum"); }
    if (f & 0x10000) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"constructor"); }
    if (f & 0x20000) { if (pos) buf[pos++]=' '; pos+=sprintf(buf+pos,"declared-synchronized"); }
    return buf;
}

char *smali_tsm(const char *d) {
    if (!d) return strdup("V");
    return strdup(d);
}

int smali_sa(smali_sb *s, const char *str) {
    size_t sl = strlen(str);
    if (*s->l + sl + 1 > *s->c) {
        *s->c = *s->c ? *s->c * 2 : 8192;
        char *n = realloc(*s->b, *s->c);
        if (!n) return -1; *s->b = n;
    }
    memcpy(*s->b + *s->l, str, sl); *s->l += sl; (*s->b)[*s->l] = 0;
    return 0;
}

int smali_sf(smali_sb *s, const char *fmt, ...) {
    va_list ap; va_start(ap, fmt);
    int n = vsnprintf(NULL, 0, fmt, ap); va_end(ap);
    if (n < 0) return -1;
    if (*s->l + (size_t)n + 1 > *s->c) {
        while (*s->c < *s->l + (size_t)n + 1) *s->c = *s->c ? *s->c * 2 : 8192;
        char *p = realloc(*s->b, *s->c); if (!p) return -1; *s->b = p;
    }
    va_start(ap, fmt); vsnprintf(*s->b + *s->l, *s->c - *s->l, fmt, ap); va_end(ap);
    *s->l += n; return 0;
}

/* Decodes MUTF-8 and escapes like baksmali: only 0x20..0x7e stay literal, the rest is \uXXXX. */
static const char *smali_escape_str(const char *raw) {
    static char *buf; static size_t cap;
    size_t need = strlen(raw) * 6 + 1;
    if (need > cap) {
        char *nb = realloc(buf, need);
        if (!nb) return "";
        buf = nb; cap = need;
    }
    size_t j = 0;
    const unsigned char *r = (const unsigned char *)raw;
    for (size_t i = 0; r[i]; ) {
        unsigned u = r[i];
        if (u >= 0xE0 && r[i+1] && r[i+2]) { u = ((u & 0x0F) << 12) | ((r[i+1] & 0x3F) << 6) | (r[i+2] & 0x3F); i += 3; }
        else if (u >= 0xC0 && r[i+1]) { u = ((u & 0x1F) << 6) | (r[i+1] & 0x3F); i += 2; }
        else i++;
        switch (u) {
        case '\b': buf[j++]='\\'; buf[j++]='b'; break;
        case '\t': buf[j++]='\\'; buf[j++]='t'; break;
        case '\n': buf[j++]='\\'; buf[j++]='n'; break;
        case '\f': buf[j++]='\\'; buf[j++]='f'; break;
        case '\r': buf[j++]='\\'; buf[j++]='r'; break;
        case '"': case '\'': case '\\': buf[j++]='\\'; buf[j++]=(char)u; break;
        default:
            if (u >= 0x20 && u <= 0x7e) buf[j++] = (char)u;
            else j += (size_t)sprintf(buf + j, "\\u%04x", u & 0xFFFF);
        }
    }
    buf[j] = '\0';
    return buf;
}

const char *smali_res(dex_ctx *ctx, int k, uint32_t i) {
    switch (k) {
    case 1: return i < ctx->string_count ? smali_escape_str(ctx->sp.strings[i]) : "??";
    case 2: return i < ctx->type_count ? ctx->sp.strings[ctx->type_ids[i]] : "??";
    case 3:
        if (i < ctx->field_count) {
            dex_field *f = &ctx->fields[i];
            char *c = smali_tsm(smali_res(ctx,2,f->class_idx));
            char *t = smali_tsm(smali_res(ctx,2,f->type_idx));
            static char b[4096];
            snprintf(b,sizeof(b), "%s->%s:%s",
                c?:smali_res(ctx,2,f->class_idx),
                smali_res(ctx,1,f->name_idx),
                t?:smali_res(ctx,2,f->type_idx));
            free(c); free(t); return b;
        } return "??";
    case 4:
        if (i < ctx->method_count) {
            dex_method_id *m = &ctx->methods[i];
            dex_proto *p = &ctx->protos[m->proto_idx];
            char *c = smali_tsm(smali_res(ctx,2,m->class_idx));
            char *rt = smali_tsm(smali_res(ctx,2,p->return_type_idx));
            static char par[65536]; par[0] = 0;
            if (p->params_off && p->params_off+4 <= ctx->size) {
                uint32_t po = p->params_off, pc;
                memcpy(&pc, ctx->data+po, 4); po += 4;
                if (po+pc*2<=ctx->size) for (uint32_t pi=0; pi<pc; pi++) {
                    uint16_t ti; memcpy(&ti, ctx->data+po+pi*2, 2);
                    const char *pt = smali_res(ctx,2,ti);
                    strncat(par,pt, sizeof(par)-strlen(par)-1);
                }
            }
            static char b[65536 + 4096];
            snprintf(b,sizeof(b), "%s->%s(%s)%s",
                c?:smali_res(ctx,2,m->class_idx),
                smali_res(ctx,1,m->name_idx), par,
                rt?:smali_res(ctx,2,p->return_type_idx));
            free(c); free(rt); return b;
        } return "??";
    default: return "";
    }
}

void smali_mproto(dex_ctx *ctx, uint32_t mi, char *b, size_t z) {
    if (mi >= ctx->method_count) { snprintf(b,z,"()V"); return; }
    dex_method_id *m = &ctx->methods[mi];
    dex_proto *p = &ctx->protos[m->proto_idx];
    const char *rt = smali_res(ctx,2,p->return_type_idx);
    static char pbuf[65536]; pbuf[0] = 0;
    if (p->params_off && p->params_off+4 <= ctx->size) {
        uint32_t po=p->params_off, pc;
        memcpy(&pc, ctx->data+po, 4); po += 4;
        if (po+pc*2 <= ctx->size) for (uint32_t i=0; i<pc; i++) {
            uint16_t ti; memcpy(&ti, ctx->data+po+i*2, 2);
            const char *pt = smali_res(ctx,2,ti);
            strncat(pbuf, pt ?: "?", sizeof(pbuf)-strlen(pbuf)-1);
        }
    }
    snprintf(b,z,"(%s)%s", pbuf, rt ?: "V");
}

const char *smali_reg_name(uint32_t r, uint32_t regs, uint32_t ins_size) {
    static char buf[4][16];
    static int idx = 0;
    int i = idx;
    idx = (idx + 1) & 3;
    uint32_t locals = regs - ins_size;
    if (r >= locals)
        snprintf(buf[i], sizeof(buf[i]), "p%u", r - locals);
    else
        snprintf(buf[i], sizeof(buf[i]), "v%u", r);
    return buf[i];
}

int smali_uleb(const uint8_t *data, uint32_t *val, const uint8_t **next) {
    *val = 0; int shift = 0, i = 0;
    while (1) {
        *val |= (uint32_t)(data[i] & 0x7F) << shift;
        if (!(data[i++] & 0x80)) break;
        shift += 7;
    }
    if (next) *next = data + i;
    return i;
}

int smali_sleb(const uint8_t *data, int32_t *val, const uint8_t **next) {
    *val = 0; int shift = 0, i = 0;
    uint8_t byte;
    while (1) {
        byte = data[i++];
        *val |= (int32_t)(byte & 0x7F) << shift;
        shift += 7;
        if (!(byte & 0x80)) break;
    }
    if (shift < 32 && (byte & 0x40))
        *val |= -(int32_t)(1 << shift);
    if (next) *next = data + i;
    return i;
}