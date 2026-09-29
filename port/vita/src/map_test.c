#include <psp2/ctrl.h>
#include <psp2/display.h>
#include <psp2/kernel/processmgr.h>
#include <psp2/io/fcntl.h>
#include <psp2/io/stat.h>
#include <math.h>
#include <vitaGL.h>
#include <stdio.h>
#include <stdarg.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include "halo_vita_memory.h"
#include "cache/physical_memory_map.h"
#include "memory/zlib/zlib.h"
#include "memory/crc.h"
#include "debugScreen.h"

#define CACHE_HEADER_SIZE 0x800u
#define HEAD_SIGNATURE 0x68656164u
#define FOOT_SIGNATURE 0x666f6f74u
#define TAGS_SIGNATURE 0x74616773u
#define XBOX_TAG_BASE 0x803A6000u
#define GAME_STATE_BYTES 0x01000000u
#define CAMPAIGN_CACHE_MAGIC 0x48433130u
#define CAMPAIGN_CACHE_VERSION 1u
#define CAMPAIGN_CACHE_PATH "ux0:data/halo-vita-diagnostic/a10-cache-v1.bin"
#define CAMPAIGN_CACHE_TEMP "ux0:data/halo-vita-diagnostic/a10-cache-v1.tmp"

unsigned int _newlib_heap_size_user = 8u * 1024u * 1024u;

struct cache_file_header {
    uint32_t header_signature;
    int32_t version;
    int32_t file_length;
    uint8_t reserved_c[4];
    int32_t tag_data_offset;
    int32_t tag_data_size;
    uint8_t reserved_18[8];
    char name[0x20];
    char build[0x20];
    uint8_t reserved_60[4];
    uint32_t checksum;
    uint8_t reserved_68[0x794];
    uint32_t footer_signature;
};

struct cache_file_tag_header {
    uint32_t tag_instances;
    int32_t scenario_tag_index;
    uint32_t checksum;
    int32_t tag_count;
    int32_t vertex_buffer_count;
    uint32_t vertex_buffers;
    int32_t index_buffer_count;
    uint32_t index_buffers;
    uint32_t signature;
};

struct cache_file_tag_instance {
    uint32_t group_tag;
    uint32_t parent_group_tags[2];
    uint32_t tag_index;
    uint32_t name;
    uint32_t base_address;
    uint32_t unused[2];
};

struct tag_block_disk { int32_t count; uint32_t address, definition; };
struct bitmap_group_disk { uint8_t pad[96]; struct tag_block_disk bitmaps; };
struct bitmap_data_disk {
    uint32_t signature; int16_t width, height, depth, type, format; uint16_t flags;
    int16_t rx, ry, mipmaps, mip_pad; int32_t pixels_offset, pixels_size, tag_index, cache_block;
    uint32_t hardware, base;
};
struct image_asset { int32_t offset, size; int16_t width, height, format; uint8_t *data; };
static uint8_t logo_pixels[0x40000];
static uint8_t menu_pixels[4][2][0x4000];
static uint8_t difficulty_header[0x8000], difficulty_pixels[4][0x10000];
static uint8_t multiplayer_header[0x8000], multiplayer_pixels[4][0x20000];

static uint8_t zlib_in[65536];
static uint8_t zlib_out[65536];

typedef char verify_cache_header_size[sizeof(struct cache_file_header) == CACHE_HEADER_SIZE ? 1 : -1];
typedef char verify_tag_header_size[sizeof(struct cache_file_tag_header) == 0x24 ? 1 : -1];
typedef char verify_tag_instance_size[sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

static FILE *log_file;
static int log_error;
static int gpu_scene_init(uint8_t *bsp, uint32_t bytes, uint32_t base);
static void gpu_scene_draw_menu_bg(float yaw);

static void report(const char *format, ...) {
    char buffer[512];
    va_list args;
    va_start(args, format);
    vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);
    psvDebugScreenPrintf("%s", buffer);
    if (log_file && (fputs(buffer, log_file) < 0 || fflush(log_file) != 0)) log_error = 1;
}

void display_assert(char *information, char *file, long line, unsigned char fatal) {
    report("HALO ASSERT fatal=%u %s:%ld %s\n", fatal, file, line, information ? information : "");
}

void system_exit(long code) {
    report("Halo requested exit=%ld\n", code);
    if (log_file) fclose(log_file);
    sceKernelExitProcess((int)code);
    for (;;) {}
}

/* The original zlib wrapper routes its tiny working allocations through the
 * Halo debug heap.  This standalone harness supplies the equivalent ABI. */
void *debug_malloc(unsigned int size, unsigned char clear, const char *file, long line) {
    (void)file; (void)line;
    return clear ? calloc(1, size) : malloc(size);
}

void debug_free(void *pointer, const char *file, long line) {
    (void)file; (void)line;
    free(pointer);
}

static int read_header(FILE *file, struct cache_file_header *header) {
    return fseek(file, 0, SEEK_SET) == 0 && fread(header, 1, sizeof(*header), file) == sizeof(*header);
}

/* Xbox DVD maps contain a normal 0x800-byte header followed by one zlib stream.
 * Offsets in the header address the decompressed file, including that header. */
static int inflate_tag_data(FILE *file, const struct cache_file_header *header, uint8_t *destination) {
    z_stream stream;
    uint32_t produced = CACHE_HEADER_SIZE;
    uint32_t wanted_begin = (uint32_t)header->tag_data_offset;
    uint32_t wanted_end = wanted_begin + (uint32_t)header->tag_data_size;
    int status = Z_OK;

    memset(&stream, 0, sizeof(stream));
    if (fseek(file, CACHE_HEADER_SIZE, SEEK_SET) != 0 || inflateInit(&stream) != Z_OK) return 0;

    while (status == Z_OK && produced < wanted_end) {
        if (stream.avail_in == 0) {
            stream.avail_in = (uInt)fread(zlib_in, 1, sizeof(zlib_in), file);
            stream.next_in = zlib_in;
            if (stream.avail_in == 0) break;
        }
        stream.next_out = zlib_out;
        stream.avail_out = sizeof(zlib_out);
        status = inflate(&stream, Z_NO_FLUSH);

        uint32_t count = (uint32_t)(sizeof(zlib_out) - stream.avail_out);
        uint32_t chunk_begin = produced;
        uint32_t chunk_end = produced + count;
        if (chunk_end > wanted_begin && chunk_begin < wanted_end) {
            uint32_t copy_begin = chunk_begin < wanted_begin ? wanted_begin : chunk_begin;
            uint32_t copy_end = chunk_end > wanted_end ? wanted_end : chunk_end;
            memcpy(destination + (copy_begin - wanted_begin), zlib_out + (copy_begin - chunk_begin), copy_end - copy_begin);
        }
        produced = chunk_end;
    }
    inflateEnd(&stream);
    report("inflate produced=%lu target_end=%lu zlib=%d\n",
        (unsigned long)produced, (unsigned long)wanted_end, status);
    return produced >= wanted_end && (status == Z_OK || status == Z_STREAM_END);
}

static int inflate_range(FILE *file,uint32_t begin,uint32_t size,uint8_t *destination) {
    z_stream s;uint32_t pos=CACHE_HEADER_SIZE,end=begin+size;int rc=Z_OK;
    memset(&s,0,sizeof(s));if(fseek(file,CACHE_HEADER_SIZE,SEEK_SET)||inflateInit(&s)!=Z_OK)return 0;
    while(rc==Z_OK&&pos<end){if(!s.avail_in){s.avail_in=fread(zlib_in,1,sizeof(zlib_in),file);s.next_in=zlib_in;if(!s.avail_in)break;}
        s.next_out=zlib_out;s.avail_out=sizeof(zlib_out);rc=inflate(&s,Z_NO_FLUSH);uint32_t n=sizeof(zlib_out)-s.avail_out,next=pos+n;
        if(next>begin&&pos<end){uint32_t cb=pos>begin?pos:begin,ce=next<end?next:end;memcpy(destination+cb-begin,zlib_out+cb-pos,ce-cb);}pos=next;}
    inflateEnd(&s);report("range inflate [%08lx,%08lx) produced=%08lx zlib=%d\n",(unsigned long)begin,(unsigned long)end,(unsigned long)pos,rc);return pos>=end;
}

struct campaign_cache_header {
    uint32_t magic, version, header_size;
    uint32_t source_checksum, source_file_length, tag_offset, tag_size;
    uint32_t bsp_offset, bsp_size, bsp_base;
    uint32_t tag_crc, bsp_crc;
};

static uint32_t buffer_crc(const void *data, uint32_t size) {
    unsigned long crc;
    crc_new(&crc);
    crc_checksum_buffer(&crc, data, (long)size);
    return (uint32_t)crc;
}

static void get_cache_paths(const struct cache_file_header *map_header, char *path, size_t psz, char *tmp, size_t tsz) {
    sceIoMkdir("ux0:data/halo-vita-diagnostic", 0777);
    snprintf(path, psz, "ux0:data/halo-vita-diagnostic/cache-%08lx.bin", (unsigned long)map_header->checksum);
    snprintf(tmp, tsz, "ux0:data/halo-vita-diagnostic/cache-%08lx.tmp", (unsigned long)map_header->checksum);
}

/* Capture both the beginning of the decompressed map and its tag cache in one
 * pass.  Xbox DVD maps are one zlib stream, so reaching the tags near EOF is
 * inherently sequential.  Progress output also proves that the Vita is alive. */
static int inflate_campaign_data(FILE *file, const struct cache_file_header *header,
    uint8_t *tags, uint8_t *map_start) {
    z_stream stream;
    uint32_t produced=CACHE_HEADER_SIZE, tag_begin=(uint32_t)header->tag_data_offset;
    uint32_t tag_end=tag_begin+(uint32_t)header->tag_data_size;
    uint32_t start_end=CACHE_HEADER_SIZE+GAME_STATE_BYTES, next_progress=32u*1024u*1024u;
    int status=Z_OK;
    memset(&stream,0,sizeof(stream));
    if(fseek(file,CACHE_HEADER_SIZE,SEEK_SET)!=0||inflateInit(&stream)!=Z_OK)return 0;
    report("map inflate 0%% (first load creates cache)\n");
    while(status==Z_OK&&produced<tag_end){
        if(!stream.avail_in){stream.avail_in=(uInt)fread(zlib_in,1,sizeof(zlib_in),file);stream.next_in=zlib_in;if(!stream.avail_in)break;}
        stream.next_out=zlib_out;stream.avail_out=sizeof(zlib_out);status=inflate(&stream,Z_NO_FLUSH);
        uint32_t count=(uint32_t)(sizeof(zlib_out)-stream.avail_out),chunk_begin=produced,chunk_end=produced+count;
        if(chunk_end>CACHE_HEADER_SIZE&&chunk_begin<start_end){uint32_t b=chunk_begin>CACHE_HEADER_SIZE?chunk_begin:CACHE_HEADER_SIZE,e=chunk_end<start_end?chunk_end:start_end;memcpy(map_start+b-CACHE_HEADER_SIZE,zlib_out+b-chunk_begin,e-b);}
        if(chunk_end>tag_begin&&chunk_begin<tag_end){uint32_t b=chunk_begin>tag_begin?chunk_begin:tag_begin,e=chunk_end<tag_end?chunk_end:tag_end;memcpy(tags+b-tag_begin,zlib_out+b-chunk_begin,e-b);}
        produced=chunk_end;
        if(produced>=next_progress){report("map inflate %lu/%lu MiB\n",(unsigned long)(produced>>20),(unsigned long)(tag_end>>20));next_progress+=32u*1024u*1024u;}
    }
    inflateEnd(&stream);
    report("map inflate produced=%lu target=%lu zlib=%d\n",(unsigned long)produced,(unsigned long)tag_end,status);
    return produced>=tag_end&&(status==Z_OK||status==Z_STREAM_END);
}

static int load_campaign_cache(const struct cache_file_header *map_header,
    uint8_t *tags, uint8_t *bsp, struct campaign_cache_header *cache) {
    char path[128], tmp[128];
    get_cache_paths(map_header, path, sizeof(path), tmp, sizeof(tmp));
    FILE *f=fopen(path,"rb");
    if(!f)return 0;
    int ok=fread(cache,1,sizeof(*cache),f)==sizeof(*cache)&&
        cache->magic==CAMPAIGN_CACHE_MAGIC&&cache->version==CAMPAIGN_CACHE_VERSION&&
        cache->header_size==sizeof(*cache)&&cache->source_checksum==map_header->checksum&&
        cache->source_file_length==(uint32_t)map_header->file_length&&
        cache->tag_offset==(uint32_t)map_header->tag_data_offset&&
        cache->tag_size==(uint32_t)map_header->tag_data_size&&
        cache->bsp_size>0&&cache->bsp_size<=GAME_STATE_BYTES;
    if(ok)ok=fread(tags,1,cache->tag_size,f)==cache->tag_size&&fread(bsp,1,cache->bsp_size,f)==cache->bsp_size;
    fclose(f);
    if(ok)ok=buffer_crc(tags,cache->tag_size)==cache->tag_crc&&buffer_crc(bsp,cache->bsp_size)==cache->bsp_crc;
    report("map cache: %s%s\n",ok?"HIT":"MISS",ok?" (no full inflate)":"");
    return ok;
}

static int save_campaign_cache(const struct cache_file_header *map_header,
    const uint8_t *tags, const uint8_t *bsp, uint32_t bsp_offset, uint32_t bsp_size, uint32_t bsp_base) {
    char path[128], tmp[128];
    get_cache_paths(map_header, path, sizeof(path), tmp, sizeof(tmp));
    struct campaign_cache_header cache={CAMPAIGN_CACHE_MAGIC,CAMPAIGN_CACHE_VERSION,sizeof(cache),
        map_header->checksum,(uint32_t)map_header->file_length,(uint32_t)map_header->tag_data_offset,
        (uint32_t)map_header->tag_data_size,bsp_offset,bsp_size,bsp_base,
        buffer_crc(tags,(uint32_t)map_header->tag_data_size),buffer_crc(bsp,bsp_size)};
    FILE *f=fopen(tmp,"wb");
    int ok=f&&fwrite(&cache,1,sizeof(cache),f)==sizeof(cache)&&
        fwrite(tags,1,cache.tag_size,f)==cache.tag_size&&fwrite(bsp,1,bsp_size,f)==bsp_size&&fflush(f)==0;
    if(f)fclose(f);
    if(ok){remove(path);ok=rename(tmp,path)==0;}
    if(!ok)remove(tmp);
    report("map cache write: %s bytes=%lu\n",ok?"PASS":"FAIL",(unsigned long)(sizeof(cache)+cache.tag_size+bsp_size));
    return ok;
}

static int inflate_assets(FILE *file, struct image_asset *a, unsigned n) {
    z_stream s; uint32_t pos=CACHE_HEADER_SIZE, end=0; int rc=Z_OK;
    for (unsigned i=0;i<n;i++) if ((uint32_t)(a[i].offset+a[i].size)>end) end=a[i].offset+a[i].size;
    memset(&s,0,sizeof(s)); if (fseek(file,CACHE_HEADER_SIZE,SEEK_SET)||inflateInit(&s)!=Z_OK) return 0;
    while (rc==Z_OK && pos<end) {
        if (!s.avail_in) { s.avail_in=fread(zlib_in,1,sizeof(zlib_in),file); s.next_in=zlib_in; if(!s.avail_in) break; }
        s.next_out=zlib_out; s.avail_out=sizeof(zlib_out); rc=inflate(&s,Z_NO_FLUSH);
        uint32_t count=sizeof(zlib_out)-s.avail_out, next=pos+count;
        for(unsigned i=0;i<n;i++) { uint32_t b=a[i].offset,e=b+a[i].size;
            if(next>b && pos<e) { uint32_t cb=pos>b?pos:b,ce=next<e?next:e;
                memcpy(a[i].data+cb-b,zlib_out+cb-pos,ce-cb); }}
        pos=next;
    }
    inflateEnd(&s); report("graphics inflate produced=%lu end=%lu zlib=%d\n",(unsigned long)pos,(unsigned long)end,rc);
    return pos>=end;
}

static void group_name(uint32_t group, char text[5]) {
    text[0] = (char)(group >> 24);
    text[1] = (char)(group >> 16);
    text[2] = (char)(group >> 8);
    text[3] = (char)group;
    text[4] = 0;
}

static void *translate_tag_pointer(uint8_t *tags, uint32_t tag_bytes, uint32_t xbox_pointer, uint32_t needed) {
    if (xbox_pointer < XBOX_TAG_BASE) return NULL;
    uint32_t offset = xbox_pointer - XBOX_TAG_BASE;
    if (offset > tag_bytes || needed > tag_bytes - offset) return NULL;
    return tags + offset;
}

static struct cache_file_tag_instance *find_tag(uint8_t *tags, uint32_t tag_bytes,
    uint32_t group_tag, const char *wanted_name) {
    struct cache_file_tag_header *header = (struct cache_file_tag_header *)tags;
    struct cache_file_tag_instance *instances = translate_tag_pointer(tags, tag_bytes,
        header->tag_instances, (uint32_t)header->tag_count * sizeof(*instances));
    if (!instances) return NULL;
    for (int32_t i = 0; i < header->tag_count; ++i) {
        const char *name = translate_tag_pointer(tags, tag_bytes, instances[i].name, 1);
        if (instances[i].group_tag == group_tag && name && strcmp(name, wanted_name) == 0)
            return &instances[i];
    }
    return NULL;
}

static int get_asset(uint8_t *tags,uint32_t bytes,const char *name,int index,uint8_t *storage,uint32_t cap,struct image_asset *a) {
    struct cache_file_tag_instance *tag=find_tag(tags,bytes,0x6269746Du,name);
    struct bitmap_group_disk *g=tag?translate_tag_pointer(tags,bytes,tag->base_address,sizeof(*g)):NULL;
    struct bitmap_data_disk *b=(g&&index>=0&&index<g->bitmaps.count)?translate_tag_pointer(tags,bytes,g->bitmaps.address,g->bitmaps.count*sizeof(*b)):NULL;
    if(!b||b[index].pixels_size<=0||(uint32_t)b[index].pixels_size>cap) return 0;
    a->offset=b[index].pixels_offset;a->size=b[index].pixels_size;a->width=b[index].width;a->height=b[index].height;a->format=b[index].format;a->data=storage;
    return a->format==14||a->format==15||a->format==16||a->format==2;
}
static uint32_t c565(uint16_t v) { return ((v>>11)&31)*255/31 | (((v>>5)&63)*255/63)<<8 | ((v&31)*255/31)<<16; }
static uint32_t texel(const struct image_asset *a,int x,int y) {
    if (a->format == 2) {
        uint8_t alpha = a->data[y * a->width + x];
        return ((uint32_t)alpha << 24) | 0x00FFFFFFu;
    }
    if (a->format == 14) {
        const uint8_t *p = a->data + ((y / 4) * ((a->width + 3) / 4) + x / 4) * 8;
        int n = (y & 3) * 4 + (x & 3);
        uint16_t v0, v1; uint32_t q;
        memcpy(&v0, p, 2); memcpy(&v1, p + 2, 2); memcpy(&q, p + 4, 4);
        uint32_t c[4] = { c565(v0), c565(v1), 0, 0 };
        if (v0 > v1) {
            for (int s = 0; s <= 16; s += 8) {
                uint32_t x0 = (c[0] >> s) & 255, x1 = (c[1] >> s) & 255;
                c[2] |= ((2 * x0 + x1) / 3) << s; c[3] |= ((x0 + 2 * x1) / 3) << s;
            }
            return c[(q >> (n * 2)) & 3] | 0xFF000000u;
        } else {
            for (int s = 0; s <= 16; s += 8) {
                uint32_t x0 = (c[0] >> s) & 255, x1 = (c[1] >> s) & 255;
                c[2] |= ((x0 + x1) / 2) << s;
            }
            c[3] = 0;
            uint32_t idx = (q >> (n * 2)) & 3;
            return idx == 3 ? 0 : (c[idx] | 0xFF000000u);
        }
    }
    const uint8_t *p=a->data+((y/4)*((a->width+3)/4)+x/4)*16; int n=(y&3)*4+(x&3); uint32_t alpha;
    if(a->format==15) { uint64_t q;memcpy(&q,p,8);alpha=((q>>(n*4))&15)*17; }
    else { uint64_t q=0;uint8_t ap[8],a0=p[0],a1=p[1];memcpy(&q,p+2,6);ap[0]=a0;ap[1]=a1;
        if(a0>a1) for(int i=2;i<8;i++) ap[i]=((7-i)*a0+(i-1)*a1)/7;
        else {for(int i=2;i<6;i++) ap[i]=((5-i)*a0+(i-1)*a1)/5;ap[6]=0;ap[7]=255;} alpha=ap[(q>>(n*3))&7];}
    uint16_t v0,v1;uint32_t q;memcpy(&v0,p+8,2);memcpy(&v1,p+10,2);memcpy(&q,p+12,4);uint32_t c[4]={c565(v0),c565(v1),0,0};
    for(int s=0;s<=16;s+=8){uint32_t x0=(c[0]>>s)&255,x1=(c[1]>>s)&255;c[2]|=((2*x0+x1)/3)<<s;c[3]|=((x0+2*x1)/3)<<s;}
    return c[(q>>(n*2))&3]|(alpha<<24);
}
static void draw_image(uint32_t *fb, int pitch, const struct image_asset *a, int dx, int dy, int dw, int dh) {
    if (!a || !a->data || dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; y++) {
        if (dy + y < 0 || dy + y >= 544) continue;
        float fy = (float)y * (float)(a->height - 1) / (float)(dh > 1 ? dh - 1 : 1);
        int iy0 = (int)fy;
        int iy1 = (iy0 + 1 < a->height) ? iy0 + 1 : iy0;
        float wy = fy - (float)iy0;

        for (int x = 0; x < dw; x++) {
            if (dx + x < 0 || dx + x >= 960) continue;
            float fx = (float)x * (float)(a->width - 1) / (float)(dw > 1 ? dw - 1 : 1);
            int ix0 = (int)fx;
            int ix1 = (ix0 + 1 < a->width) ? ix0 + 1 : ix0;
            float wx = fx - (float)ix0;

            uint32_t c00 = texel(a, ix0, iy0);
            uint32_t c10 = texel(a, ix1, iy0);
            uint32_t c01 = texel(a, ix0, iy1);
            uint32_t c11 = texel(a, ix1, iy1);

            float w00 = (1.0f - wx) * (1.0f - wy);
            float w10 = wx * (1.0f - wy);
            float w01 = (1.0f - wx) * wy;
            float w11 = wx * wy;

            uint32_t r = (uint32_t)((c00 & 0xFF) * w00 + (c10 & 0xFF) * w10 + (c01 & 0xFF) * w01 + (c11 & 0xFF) * w11);
            uint32_t g = (uint32_t)(((c00 >> 8) & 0xFF) * w00 + ((c10 >> 8) & 0xFF) * w10 + ((c01 >> 8) & 0xFF) * w01 + ((c11 >> 8) & 0xFF) * w11);
            uint32_t b = (uint32_t)(((c00 >> 16) & 0xFF) * w00 + ((c10 >> 16) & 0xFF) * w10 + ((c01 >> 16) & 0xFF) * w01 + ((c11 >> 16) & 0xFF) * w11);
            uint32_t al = (uint32_t)(((c00 >> 24) & 0xFF) * w00 + ((c10 >> 24) & 0xFF) * w10 + ((c01 >> 24) & 0xFF) * w01 + ((c11 >> 24) & 0xFF) * w11);

            if (al < 4) continue;
            uint32_t *d = &fb[(dy + y) * pitch + (dx + x)];
            uint32_t orig = *d;

            uint32_t dr = (r * al + (orig & 0xFF) * (255 - al)) / 255;
            uint32_t dg = (g * al + ((orig >> 8) & 0xFF) * (255 - al)) / 255;
            uint32_t db = (b * al + ((orig >> 16) & 0xFF) * (255 - al)) / 255;

            *d = 0xFF000000u | dr | (dg << 8) | (db << 16);
        }
    }
}

static int run_ui_bootstrap(FILE *map,uint8_t *tags, uint32_t tag_bytes) {
    static const char *labels[] = {"CAMPANA", "MULTIJUGADOR", "CONFIGURACION", "DEMOS"};
    static const char *paths[] = {
        "ui\\shell\\main_menu\\main_menu_item_load_camp",
        "ui\\shell\\main_menu\\main_menu_item_multiplayer",
        "ui\\shell\\main_menu\\main_menu_item_settings",
        "ui\\shell\\main_menu\\main_menu_item_game_demos"
    };
    struct cache_file_tag_instance *items[4];
    static const char *bitmap_paths[]={"ui\\shell\\main_menu\\menu_load_campaign","ui\\shell\\main_menu\\menu_multiplayer","ui\\shell\\main_menu\\menu_settings","ui\\shell\\main_menu\\menu_game_demos"};
    struct image_asset assets[9];
    for (unsigned i = 0; i < 4; ++i) {
        items[i] = find_tag(tags, tag_bytes, 0x44654C61u, paths[i]);
        if (!items[i]) {
            report("menu tag missing: %s\n", paths[i]);
            return 0;
        }
        report("menu tag %s datum=%08lx: PASS\n", labels[i], (unsigned long)items[i]->tag_index);
    }
    if(!get_asset(tags,tag_bytes,"ui\\shell\\main_menu\\halo_logo",0,logo_pixels,sizeof(logo_pixels),&assets[0])) return 0;
    for(unsigned i=0;i<4;i++)for(unsigned s=0;s<2;s++)if(!get_asset(tags,tag_bytes,bitmap_paths[i],s,menu_pixels[i][s],sizeof(menu_pixels[i][s]),&assets[1+i*2+s]))return 0;
    if(!inflate_assets(map,assets,9))return 0;
    report("original graphics logo=%dx%d fmt=%d menu=%dx%d fmt=%d: PASS\n",assets[0].width,assets[0].height,assets[0].format,assets[1].width,assets[1].height,assets[1].format);
    SceDisplayFrameBuf fb={0};fb.size=sizeof(fb);if(sceDisplayGetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME)<0||!fb.base)return 0;

    int selected = 0;
    int old_selected = 0;
    uint32_t previous = 0;
    int redraw = 2;
    for (;;) {
        SceCtrlData pad = {0};
        sceCtrlPeekBufferPositive(0, &pad, 1);
        uint32_t pressed = pad.buttons & ~previous;
        previous = pad.buttons;
        if ((pad.buttons & (SCE_CTRL_SELECT | SCE_CTRL_START)) == (SCE_CTRL_SELECT | SCE_CTRL_START)) break;
        if (pressed & SCE_CTRL_UP) { old_selected=selected; selected = (selected + 3) % 4; redraw = 1; }
        if (pressed & SCE_CTRL_DOWN) { old_selected=selected; selected = (selected + 1) % 4; redraw = 1; }
        if (pressed & SCE_CTRL_CROSS) {
            report("selected %s datum=%08lx path=%s\n", labels[selected],
                (unsigned long)items[selected]->tag_index, paths[selected]);
        }
        if (redraw) {
            uint32_t *frame=fb.base;
            if(redraw==2) {
                for(int y=0;y<544;y++)for(int x=0;x<960;x++){uint32_t b=8+y*24/544,g=3+y*10/544;frame[y*(int)fb.pitch+x]=0xff000000u|(b<<16)|(g<<8);}
                draw_image(frame,fb.pitch,&assets[0],120,35,720,180);
                for(int i=0;i<4;i++)draw_image(frame,fb.pitch,&assets[1+i*2+(i==selected)],352,245+i*62,256,64);
            } else {
                int slots[2]={old_selected,selected};
                for(int k=0;k<2;k++){int slot=slots[k],top=245+slot*62;
                    for(int y=top;y<top+64;y++)for(int x=352;x<608;x++){uint32_t b=8+y*24/544,g=3+y*10/544;frame[y*(int)fb.pitch+x]=0xff000000u|(b<<16)|(g<<8);}
                    draw_image(frame,fb.pitch,&assets[1+slot*2+(slot==selected)],352,top,256,64);
                }
            }
            redraw = 0;
        }
        sceDisplayWaitVblankStart();
    }
    return 1;
}

static void probe_campaign_map(void) {
    const char *paths[]={
        "ux0:data/halo/maps/bloodgulch.map","ux0:data/halo/maps_es/bloodgulch.map",
        "ux0:data/halo-vita/maps/bloodgulch.map","ux0:data/halo-vita/maps_es/bloodgulch.map",
        "ux0:data/halo/maps/beavercreek.map","ux0:data/halo/maps_es/beavercreek.map",
        "ux0:data/halo-vita/maps/beavercreek.map","ux0:data/halo-vita/maps_es/beavercreek.map",
        "ux0:data/halo/maps/a10.map","ux0:data/halo/maps_es/a10.map",
        "ux0:data/halo-vita/maps/a10.map","ux0:data/halo-vita/maps_es/a10.map"
    };
    struct cache_file_header h; FILE *f=NULL; const char *path=NULL;
    for(unsigned i=0;i<sizeof(paths)/sizeof(paths[0]);i++)if((f=fopen(paths[i],"rb"))){path=paths[i];break;}
    if(!f){report("campaign launch: map missing\n");return;}
    int ok=read_header(f,&h)&&h.header_signature==HEAD_SIGNATURE&&h.footer_signature==FOOT_SIGNATURE&&h.version==5;
    fclose(f);report("campaign launch: %s %s build=%s bytes=%ld\n",path,ok?"READY":"INVALID",h.build,(long)h.file_length);
}
struct scenario_bsp_reference_disk {
    int32_t file_offset,file_size;uint32_t base_address,unused;
    uint32_t group_tag,name;int32_t name_length,tag_index;
};
static uint8_t *scene_tags;
static uint32_t scene_tag_bytes;
static const char *scene_map_path;

struct font_char_disk {
    int16_t character;
    int16_t character_width;
    int16_t bitmap_width;
    int16_t bitmap_height;
    int16_t origin_x;
    int16_t origin_y;
    int16_t hw_index;
    uint16_t pad;
    int32_t pixels_offset;
};

struct halo_font {
    int asc, desc;
    int char_count;
    const int16_t *indices;
    const struct font_char_disk *chars;
    const uint8_t *pixels;
};

static int get_font(uint8_t *tags, uint32_t tag_bytes, const char *name, struct halo_font *f) {
    struct cache_file_tag_instance *tag = find_tag(tags, tag_bytes, 0x666f6e74u, name);
    if (!tag) return 0;
    uint8_t *base = translate_tag_pointer(tags, tag_bytes, tag->base_address, 160);
    if (!base) return 0;
    f->asc = *(int16_t*)(base + 4);
    f->desc = *(int16_t*)(base + 6);
    uint32_t tbl_cnt = *(uint32_t*)(base + 48);
    uint32_t tbl_addr = *(uint32_t*)(base + 52);
    if (tbl_cnt == 0) return 0;
    uint8_t *tbl = translate_tag_pointer(tags, tag_bytes, tbl_addr, 12);
    if (!tbl) return 0;
    uint32_t ind_addr = *(uint32_t*)(tbl + 4);
    f->indices = (const int16_t*)translate_tag_pointer(tags, tag_bytes, ind_addr, 256 * sizeof(int16_t));
    uint32_t chars_cnt = *(uint32_t*)(base + 124);
    uint32_t chars_addr = *(uint32_t*)(base + 128);
    f->char_count = (int)chars_cnt;
    f->chars = (const struct font_char_disk*)translate_tag_pointer(tags, tag_bytes, chars_addr, chars_cnt * sizeof(struct font_char_disk));
    uint32_t pix_size = *(uint32_t*)(base + 136);
    uint32_t pix_addr = *(uint32_t*)(base + 148);
    f->pixels = (const uint8_t*)translate_tag_pointer(tags, tag_bytes, pix_addr, pix_size);
    return f->indices && f->chars && f->pixels;
}

static void draw_text(uint32_t *fb, int pitch, const struct halo_font *font, const char *text, int x, int y, uint32_t color) {
    if (!font || !font->indices || !font->chars || !font->pixels || !text) return;
    uint32_t cr = color & 0xFF;
    uint32_t cg = (color >> 8) & 0xFF;
    uint32_t cb = (color >> 16) & 0xFF;
    int cur_x = x;
    while (*text) {
        unsigned char c = (unsigned char)*text++;
        if (c == '\n') {
            cur_x = x;
            y += font->asc + font->desc + 5;
            continue;
        }
        if (c == ' ') {
            cur_x += 8;
            continue;
        }
        int idx = font->indices[c];
        if (idx < 0 || idx >= font->char_count) continue;
        const struct font_char_disk *ch = &font->chars[idx];
        int gx = cur_x + ch->origin_x;
        int gy = y - ch->origin_y;
        const uint8_t *src = font->pixels + ch->pixels_offset;
        for (int py = 0; py < ch->bitmap_height; py++) {
            int dy = gy + py;
            if (dy < 0 || dy >= 544) continue;
            for (int px = 0; px < ch->bitmap_width; px++) {
                int dx = gx + px;
                if (dx < 0 || dx >= 960) continue;
                uint32_t a = src[py * ch->bitmap_width + px];
                if (a < 10) continue;
                uint32_t *dst = &fb[dy * pitch + dx];
                uint32_t orig = *dst;
                uint32_t inv = 255 - a;
                uint32_t dr = (cr * a + (orig & 0xFF) * inv) / 255;
                uint32_t dg = (cg * a + ((orig >> 8) & 0xFF) * inv) / 255;
                uint32_t db = (cb * a + ((orig >> 16) & 0xFF) * inv) / 255;
                *dst = 0xFF000000u | dr | (dg << 8) | (db << 16);
            }
        }
        cur_x += ch->character_width;
    }
}

static int run_ui_08(FILE *map,uint8_t *tags,uint32_t tag_bytes) {
    static const char *main_paths[]={"ui\\shell\\main_menu\\menu_load_campaign","ui\\shell\\main_menu\\menu_multiplayer","ui\\shell\\main_menu\\menu_settings","ui\\shell\\main_menu\\menu_game_demos"};
    struct image_asset a[19];
    if(!get_asset(tags,tag_bytes,"ui\\shell\\main_menu\\halo_logo",0,logo_pixels,sizeof(logo_pixels),&a[0]))return 0;
    for(int i=0;i<4;i++)for(int s=0;s<2;s++)if(!get_asset(tags,tag_bytes,main_paths[i],s,menu_pixels[i][s],sizeof(menu_pixels[i][s]),&a[1+i*2+s]))return 0;
    if(!get_asset(tags,tag_bytes,"ui\\shell\\main_menu\\difficulty_select\\header_choose_difficulty",0,difficulty_header,sizeof(difficulty_header),&a[9]))return 0;
    for(int i=0;i<4;i++)if(!get_asset(tags,tag_bytes,"ui\\shell\\main_menu\\difficulty_select\\difficulty_options",i,difficulty_pixels[i],sizeof(difficulty_pixels[i]),&a[10+i]))return 0;
    if(!get_asset(tags,tag_bytes,"ui\\shell\\main_menu\\multiplayer_type_select\\header_multiplayer",0,multiplayer_header,sizeof(multiplayer_header),&a[14]))return 0;
    for(int i=0;i<4;i++)if(!get_asset(tags,tag_bytes,"ui\\shell\\main_menu\\multiplayer_type_select\\mp_options",i,multiplayer_pixels[i],sizeof(multiplayer_pixels[i]),&a[15+i]))return 0;
    if(!inflate_assets(map,a,19))return 0;

    struct halo_font font_large = {0}, font_small = {0};
    get_font(tags, tag_bytes, "ui\\large_ui", &font_large);
    get_font(tags, tag_bytes, "ui\\small_ui", &font_small);
    report("UI routes: main + difficulty + multiplayer graphics + fonts: PASS\n");

    SceDisplayFrameBuf fb={0};fb.size=sizeof(fb);if(sceDisplayGetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME)<0||!fb.base)return 0;
    uint32_t *frame=fb.base,previous=0;int screen=0,selected[3]={0,0,0},old=0,redraw=2;
    for(;;){SceCtrlData pad={0};sceCtrlPeekBufferPositive(0,&pad,1);uint32_t pressed=pad.buttons&~previous;previous=pad.buttons;
        if((pad.buttons&(SCE_CTRL_SELECT|SCE_CTRL_START))==(SCE_CTRL_SELECT|SCE_CTRL_START))break;
        if((pressed&SCE_CTRL_CIRCLE)&&screen){screen=0;redraw=2;report("ui back -> main menu\n");}
        if(pressed&SCE_CTRL_UP){old=selected[screen];selected[screen]=(selected[screen]+3)%4;redraw=screen?2:1;}
        if(pressed&SCE_CTRL_DOWN){old=selected[screen];selected[screen]=(selected[screen]+1)%4;redraw=screen?2:1;}
        if(pressed&SCE_CTRL_CROSS){
            if(screen==0&&selected[0]==0){screen=1;redraw=2;report("ui route -> difficulty select\n");}
            else if(screen==0&&selected[0]==1){screen=2;redraw=2;report("ui route -> multiplayer type select\n");}
            else if(screen==0)report("ui route pending subsystem item=%d\n",selected[0]);
            else if(screen==1){report("difficulty confirmed index=%d; launching campaign a10\n",selected[1]);return 2;}
            else if(screen==2){report("multiplayer type confirmed index=%d; launching bloodgulch\n",selected[2]);return 3;}
        }
        if (redraw) {
            uint32_t *frame = fb.base;
            for (int y = 0; y < 544; y++) {
                for (int x = 0; x < 960; x++) {
                    uint32_t b = 8 + y * 24 / 544, g = 3 + y * 10 / 544;
                    frame[y * (int)fb.pitch + x] = 0xFF000000u | (b << 16) | (g << 8);
                }
            }
            if (screen == 0) {
                draw_image(frame, fb.pitch, &a[0], 120, 30, 720, 170);
                for (int i = 0; i < 4; i++) {
                    draw_image(frame, fb.pitch, &a[1 + i * 2 + (i == selected[0])], 352, 235 + i * 62, 256, 60);
                }
            } else if (screen == 1) {
                draw_image(frame, fb.pitch, &a[0], 260, 12, 440, 95);
                draw_image(frame, fb.pitch, &a[9], 80, 115, 380, 42);

                static const char *diff_names[] = {"FACIL", "NORMAL", "HEROICO", "LEGENDARIO"};
                static const char *diff_descs[] = {
                    "Tus enemigos se encogen y caen ante tu imparable\nataque, pero la victoria final te sabra a poco.",
                    "Hordas de alienigenas quieren destruirte, pero con\ntus nervios de acero y tu rapido gatillo podras vencer.",
                    "Tus enemigos son tan numerosos como feroces: sus\nataques son devastadores. Supervivencia no garantizada.",
                    "Te enfrentas a enemigos que jamas han conocido\nla derrota y se burlan de tus esfuerzos."
                };

                for (int i = 0; i < 4; i++) {
                    int bx = 80, by = 175 + i * 58;
                    int bw = 380, bh = 46;
                    int is_sel = (i == selected[1]);
                    uint32_t bg_col = is_sel ? 0x60002844u : 0x40001018u;
                    uint32_t border_col = is_sel ? 0xFF00E6FFu : 0x80305070u;
                    for (int y = by; y < by + bh; y++) {
                        for (int x = bx; x < bx + bw; x++) {
                            if (x >= 0 && x < 960 && y >= 0 && y < 544) {
                                if (x == bx || x == bx + bw - 1 || y == by || y == by + bh - 1 ||
                                    (is_sel && (x <= bx + 2 || x >= bx + bw - 3 || y <= by + 2 || y >= by + bh - 3))) {
                                    frame[y * (int)fb.pitch + x] = border_col;
                                } else {
                                    uint32_t orig = frame[y * (int)fb.pitch + x];
                                    uint32_t a = (bg_col >> 24) & 0xFF;
                                    uint32_t inv = 255 - a;
                                    uint32_t r = (((bg_col & 0xFF) * a) + ((orig & 0xFF) * inv)) / 255;
                                    uint32_t g = ((((bg_col >> 8) & 0xFF) * a) + (((orig >> 8) & 0xFF) * inv)) / 255;
                                    uint32_t b = ((((bg_col >> 16) & 0xFF) * a) + (((orig >> 16) & 0xFF) * inv)) / 255;
                                    frame[y * (int)fb.pitch + x] = 0xFF000000u | r | (g << 8) | (b << 16);
                                }
                            }
                        }
                    }
                    uint32_t txt_col = is_sel ? 0xFFFFFFFFu : 0xFF8CB4D2u;
                    draw_text(frame, fb.pitch, &font_large, diff_names[i], bx + 24, by + 30, txt_col);
                }

                int emblem_idx = selected[1];
                if (emblem_idx >= 0 && emblem_idx < 4) {
                    draw_image(frame, fb.pitch, &a[10 + emblem_idx], 560, 140, 256, 256);
                    draw_text(frame, fb.pitch, &font_small, diff_descs[emblem_idx], 490, 425, 0xFFE0F4FFu);
                }
            } else if (screen == 2) {
                draw_image(frame, fb.pitch, &a[0], 260, 12, 440, 95);
                draw_image(frame, fb.pitch, &a[14], 80, 115, 380, 42);

                static const char *mp_names[] = {
                    "INICIO RAPIDO",
                    "PARTIDA EN EQUIPO",
                    "PANTALLA DIVIDIDA",
                    "INTERCONEXION",
                    "EDITAR TIPOS"
                };
                static const char *mp_descs[] = {
                    "Juega la campana individual con un amigo.",
                    "Enfrentate a cuatro personas unas contra otras\ncon un solo televisor.",
                    "Juega en modo multijugador en pantalla dividida.",
                    "Conecta hasta cuatro sistemas para un total de\nhasta dieciseis jugadores.",
                    "Configura todos los atributos de tus partidas."
                };

                for (int i = 0; i < 4; i++) {
                    int bx = 80, by = 175 + i * 58;
                    int bw = 380, bh = 46;
                    int is_sel = (i == selected[2]);
                    uint32_t bg_col = is_sel ? 0x60002844u : 0x40001018u;
                    uint32_t border_col = is_sel ? 0xFF00E6FFu : 0x80305070u;
                    for (int y = by; y < by + bh; y++) {
                        for (int x = bx; x < bx + bw; x++) {
                            if (x >= 0 && x < 960 && y >= 0 && y < 544) {
                                if (x == bx || x == bx + bw - 1 || y == by || y == by + bh - 1 ||
                                    (is_sel && (x <= bx + 2 || x >= bx + bw - 3 || y <= by + 2 || y >= by + bh - 3))) {
                                    frame[y * (int)fb.pitch + x] = border_col;
                                } else {
                                    uint32_t orig = frame[y * (int)fb.pitch + x];
                                    uint32_t a = (bg_col >> 24) & 0xFF;
                                    uint32_t inv = 255 - a;
                                    uint32_t r = (((bg_col & 0xFF) * a) + ((orig & 0xFF) * inv)) / 255;
                                    uint32_t g = ((((bg_col >> 8) & 0xFF) * a) + (((orig >> 8) & 0xFF) * inv)) / 255;
                                    uint32_t b = ((((bg_col >> 16) & 0xFF) * a) + (((orig >> 16) & 0xFF) * inv)) / 255;
                                    frame[y * (int)fb.pitch + x] = 0xFF000000u | r | (g << 8) | (b << 16);
                                }
                            }
                        }
                    }
                    uint32_t txt_col = is_sel ? 0xFFFFFFFFu : 0xFF8CB4D2u;
                    draw_text(frame, fb.pitch, &font_large, mp_names[i], bx + 24, by + 30, txt_col);
                }

                int mp_idx = selected[2];
                if (mp_idx >= 0 && mp_idx < 4) {
                    draw_image(frame, fb.pitch, &a[15 + mp_idx], 520, 160, 360, 200);
                    draw_text(frame, fb.pitch, &font_small, mp_descs[mp_idx], 490, 400, 0xFFE0F4FFu);
                }
            }
            redraw = 0;
        }
        sceDisplayWaitVblankStart();
    }
    return 1;
}

struct structure_bsp_header_disk {
    uint32_t base_address;int32_t vertex_count;uint32_t vertex_buffers;
    int32_t index_count;uint32_t index_buffers,signature;
};

struct collision_vertex_disk { float x,y,z; int32_t first_edge; };
struct collision_edge_disk { int32_t vertex[2],edge[2],surface[2]; };
struct collision_surface_disk { int32_t plane,first_edge;uint8_t flags,breakable;int16_t material; };
struct plane3d_disk { float x,y,z,d; };
struct player_start_disk { float x,y,z,facing;int16_t team,bsp;uint8_t unused[0x20]; };
struct collision_triangle { uint32_t vertex[3];uint16_t plane;int16_t material; };

static void *translate_bsp_pointer(uint8_t *bsp,uint32_t bytes,uint32_t xbox_base,uint32_t pointer,uint32_t needed){
    if(pointer<xbox_base)return NULL;
    uint32_t offset=pointer-xbox_base;
    if(offset>bytes||needed>bytes-offset)return NULL;
    return bsp+offset;
}

static void draw_line(uint32_t *frame,int pitch,int x0,int y0,int x1,int y1,uint32_t color){
    int width=pitch,height=pitch==240?136:544,dx=abs(x1-x0),sx=x0<x1?1:-1,dy=-abs(y1-y0),sy=y0<y1?1:-1,err=dx+dy;
    for(;;){if((unsigned)x0<(unsigned)width&&(unsigned)y0<(unsigned)height)frame[y0*pitch+x0]=color;if(x0==x1&&y0==y1)break;int e2=err*2;if(e2>=dy){err+=dy;x0+=sx;}if(e2<=dx){err+=dx;y0+=sy;}}
}

static int orient2d(int ax,int ay,int bx,int by,int px,int py){return (bx-ax)*(py-ay)-(by-ay)*(px-ax);}

static void fill_triangle_fast(uint32_t *color,float *zbuffer,
    int x0,int y0,float z0,int x1,int y1,float z1,int x2,int y2,float z2,uint32_t fill){
    int area=orient2d(x0,y0,x1,y1,x2,y2);if(!area)return;
    int minx=x0<x1?x0:x1;if(x2<minx)minx=x2;int maxx=x0>x1?x0:x1;if(x2>maxx)maxx=x2;
    int miny=y0<y1?y0:y1;if(y2<miny)miny=y2;int maxy=y0>y1?y0:y1;if(y2>maxy)maxy=y2;
    if(minx<0)minx=0;if(miny<0)miny=0;if(maxx>239)maxx=239;if(maxy>135)maxy=135;if(minx>maxx||miny>maxy)return;
    int sign=area>0?1:-1;
    int e0_row=orient2d(x1,y1,x2,y2,minx,miny)*sign,e1_row=orient2d(x2,y2,x0,y0,minx,miny)*sign,e2_row=orient2d(x0,y0,x1,y1,minx,miny)*sign;
    int e0_dx=-(y2-y1)*sign,e1_dx=-(y0-y2)*sign,e2_dx=-(y1-y0)*sign;
    int e0_dy=(x2-x1)*sign,e1_dy=(x0-x2)*sign,e2_dy=(x1-x0)*sign;
    float q0=1.0f/z0,q1=1.0f/z1,q2=1.0f/z2,inv_area=1.0f/(float)area;
    float dqdx=((q1-q0)*(float)(y2-y0)-(q2-q0)*(float)(y1-y0))*inv_area;
    float dqdy=((float)(x1-x0)*(q2-q0)-(float)(x2-x0)*(q1-q0))*inv_area;
    float q_row=q0+dqdx*(float)(minx-x0)+dqdy*(float)(miny-y0);
    for(int y=miny;y<=maxy;y++){
        int e0=e0_row,e1=e1_row,e2=e2_row,offset=y*240+minx;float q=q_row;
        for(int x=minx;x<=maxx;x++,offset++,q+=dqdx){if(e0>=0&&e1>=0&&e2>=0&&q>zbuffer[offset]){zbuffer[offset]=q;color[offset]=fill;}e0+=e0_dx;e1+=e1_dx;e2+=e2_dx;}
        e0_row+=e0_dy;e1_row+=e1_dy;e2_row+=e2_dy;q_row+=dqdy;
    }
}
/* Precompute walkable projected triangles once: bounds reject distant surfaces
 * before barycentric evaluation, and the hot path performs no divisions. */
struct ground_face {
    float minx,maxx,miny,maxy,ax,ay,bx,by,cx,cy,za,zb,zc,inv_area;
};
static struct ground_face *ground_faces;
static int ground_count;
static int prepare_ground(const struct collision_triangle *triangles,int count,
    const struct collision_vertex_disk *vertices){
    ground_faces=malloc((size_t)count*sizeof(*ground_faces));ground_count=0;
    if(!ground_faces)return 0;
    for(int i=0;i<count;i++){
        const struct collision_vertex_disk *a=&vertices[triangles[i].vertex[0]],*b=&vertices[triangles[i].vertex[1]],*c=&vertices[triangles[i].vertex[2]];
        float ux=b->x-a->x,uy=b->y-a->y,uz=b->z-a->z;
        float vx=c->x-a->x,vy=c->y-a->y,vz=c->z-a->z;
        float nx=uy*vz-uz*vy,ny=uz*vx-ux*vz,nz=ux*vy-uy*vx;
        /* Reject degenerate faces and slopes steeper than 45 degrees. */
        if(fabsf(nz)<0.00001f||nz*nz<nx*nx+ny*ny)continue;
        struct ground_face *g=&ground_faces[ground_count++];
        g->minx=fminf(a->x,fminf(b->x,c->x))-.001f;g->maxx=fmaxf(a->x,fmaxf(b->x,c->x))+.001f;
        g->miny=fminf(a->y,fminf(b->y,c->y))-.001f;g->maxy=fmaxf(a->y,fmaxf(b->y,c->y))+.001f;
        g->ax=a->x;g->ay=a->y;g->bx=b->x;g->by=b->y;g->cx=c->x;g->cy=c->y;
        g->za=a->z;g->zb=b->z;g->zc=c->z;g->inv_area=1.0f/nz;
    }
    report("ground cache walkable=%d total=%d\n",ground_count,count);
    return 1;
}
static int find_ground(const struct collision_triangle *triangles,int32_t triangle_count,
    const struct collision_vertex_disk *vertices,float x,float y,float ceiling,float *height){
    (void)triangles;(void)triangle_count;(void)vertices;
    float best=-1.0e30f;int found=0;
    for(int i=0;i<ground_count;i++){
        const struct ground_face *g=&ground_faces[i];
        if(x<g->minx||x>g->maxx||y<g->miny||y>g->maxy)continue;
        float w0=((g->bx-x)*(g->cy-y)-(g->by-y)*(g->cx-x))*g->inv_area;
        float w1=((g->cx-x)*(g->ay-y)-(g->cy-y)*(g->ax-x))*g->inv_area,w2=1.0f-w0-w1;
        if(w0<-.001f||w1<-.001f||w2<-.001f)continue;
        float z=w0*g->za+w1*g->zb+w2*g->zc;
        if(z<=ceiling&&z>best){best=z;found=1;}
    }
    if(found)*height=best;
    return found;
}


/* Two-sided segment/triangle intersection. No dependence on surface winding. */
static int segment_blocked(const struct collision_triangle *tris,int count,
 const struct collision_vertex_disk *v,float x,float y,float z,float dx,float dy,float dz){
 float minx=fminf(x,x+dx),maxx=fmaxf(x,x+dx),miny=fminf(y,y+dy),maxy=fmaxf(y,y+dy),minz=fminf(z,z+dz),maxz=fmaxf(z,z+dz);
 for(int i=0;i<count;i++){
  const struct collision_vertex_disk *a=&v[tris[i].vertex[0]],*b=&v[tris[i].vertex[1]],*c=&v[tris[i].vertex[2]];
  if((a->x<minx&&b->x<minx&&c->x<minx)||(a->x>maxx&&b->x>maxx&&c->x>maxx)||
     (a->y<miny&&b->y<miny&&c->y<miny)||(a->y>maxy&&b->y>maxy&&c->y>maxy)||
     (a->z<minz&&b->z<minz&&c->z<minz)||(a->z>maxz&&b->z>maxz&&c->z>maxz))continue;
  float ux=b->x-a->x,uy=b->y-a->y,uz=b->z-a->z,vx=c->x-a->x,vy=c->y-a->y,vz=c->z-a->z;
  float px=dy*vz-dz*vy,py=dz*vx-dx*vz,pz=dx*vy-dy*vx,det=ux*px+uy*py+uz*pz;
  if(fabsf(det)<1e-8f)continue;
  float inv=1.0f/det,tx=x-a->x,ty=y-a->y,tz=z-a->z,u=(tx*px+ty*py+tz*pz)*inv;
  if(u<0||u>1)continue;
  float qx=ty*uz-tz*uy,qy=tz*ux-tx*uz,qz=tx*uy-ty*ux,w=(dx*qx+dy*qy+dz*qz)*inv;
  if(w<0||u+w>1)continue;
  float t=(vx*qx+vy*qy+vz*qz)*inv;
  if(t>=0&&t<=1)return 1;
 }
 return 0;
}
static int body_blocked(const struct collision_triangle *t,int n,const struct collision_vertex_disk *v,
 float x,float y,float foot,float dx,float dy){
 float len=sqrtf(dx*dx+dy*dy);if(len<1e-6f)return 0;
 float ex=dx*(1.0f+.12f/len),ey=dy*(1.0f+.12f/len);
 return segment_blocked(t,n,v,x,y,foot+.35f,ex,ey,0)||
        segment_blocked(t,n,v,x,y,foot+.55f,ex,ey,0)||
        segment_blocked(t,n,v,x,y,foot+.75f,ex,ey,0);
}

struct gpu_vertex { float x,y,z;uint32_t color;float u,v; };
struct gpu_batch {int first,count,texture;};
static struct gpu_batch scene_batches[512];
static int scene_batch_count;
static struct image_asset scene_images[96];
static uint32_t scene_bitmap_ids[96];
static GLuint scene_textures[96];
static int scene_texture_count;
static GLuint tex_hud_reticle;
static GLuint tex_hud_weapon;
static GLuint tex_hud_unit_bg;
static GLuint tex_hud_weap_bg;
static GLuint tex_hud_blip;

static int add_hud_bitmap_asset(const char *name, int sub_idx) {
 if(scene_texture_count>=96)return -1;
 struct cache_file_tag_instance *tag=find_tag(scene_tags,scene_tag_bytes,0x6269746du,name);
 if(!tag)return -1;
 struct bitmap_group_disk *g=translate_tag_pointer(scene_tags,scene_tag_bytes,tag->base_address,sizeof(*g));
 if(!g||sub_idx<0||sub_idx>=g->bitmaps.count)return -1;
 struct bitmap_data_disk *b=translate_tag_pointer(scene_tags,scene_tag_bytes,g->bitmaps.address+sub_idx*sizeof(*b),sizeof(*b));
 if(!b||b->pixels_size<=0)return -1;
 int idx=scene_texture_count++;
 scene_bitmap_ids[idx]=tag->tag_index;
 scene_images[idx]=(struct image_asset){b->pixels_offset,b->pixels_size,b->width,b->height,b->format,NULL};
 return idx;
}

static int scene_texture_for_material(uint32_t shader_id){
 struct cache_file_tag_header *h=(void*)scene_tags;
 uint32_t slot=shader_id&65535;
 if(slot>=(uint32_t)h->tag_count)return -1;
 struct cache_file_tag_instance *t=translate_tag_pointer(scene_tags,scene_tag_bytes,h->tag_instances,h->tag_count*32);
 if(!t||t[slot].tag_index!=shader_id||t[slot].group_tag!=0x73656e76)return -1;
 uint8_t *shader=translate_tag_pointer(scene_tags,scene_tag_bytes,t[slot].base_address,0x98);
 if(!shader)return -1;
 uint32_t bitmap;memcpy(&bitmap,shader+0x94,4);
 for(int i=0;i<scene_texture_count;i++)if(scene_bitmap_ids[i]==bitmap)return i;
 slot=bitmap&65535;
 if(slot>=(uint32_t)h->tag_count||t[slot].tag_index!=bitmap||t[slot].group_tag!=0x6269746d||scene_texture_count==96)return -1;
 struct bitmap_group_disk *g=translate_tag_pointer(scene_tags,scene_tag_bytes,t[slot].base_address,sizeof(*g));
 struct bitmap_data_disk *b=(g&&g->bitmaps.count>0)?translate_tag_pointer(scene_tags,scene_tag_bytes,g->bitmaps.address,sizeof(*b)):NULL;
 if(!b||b->width<1||b->height<1||b->width>2048||b->height>2048||b->type!=0||b->format<14||b->format>16)return -1;
 int bytes=((b->width+3)/4)*((b->height+3)/4)*(b->format==14?8:16);
 if(b->pixels_offset<0||b->pixels_size<bytes)return -1;
 int i=scene_texture_count++;scene_bitmap_ids[i]=bitmap;
 scene_images[i]=(struct image_asset){b->pixels_offset,bytes,b->width,b->height,b->format,NULL};
 return i;
}
static int scene_upload_textures(void){
 int ok=1;
 int idx_reticle = add_hud_bitmap_asset("ui\\hud\\bitmaps\\combined\\hud_reticles", 0);
 int idx_weapon = add_hud_bitmap_asset("weapons\\assault rifle\\bitmaps\\assault rifle_card", 0);
 int idx_unit_bg = add_hud_bitmap_asset("ui\\hud\\bitmaps\\combined\\hud_unit_backgrounds", 0);
 int idx_weap_bg = add_hud_bitmap_asset("ui\\hud\\bitmaps\\combined\\hud_weapon_backgrounds", 0);
 int idx_blip = add_hud_bitmap_asset("ui\\hud\\bitmaps\\hud_sensor_blip", 0);
 int first_hud_tex = scene_texture_count - 5;

 for(int i=0;i<scene_texture_count;i++){scene_images[i].data=vglMalloc(scene_images[i].size);if(!scene_images[i].data)ok=0;}
 FILE *f=ok?fopen(scene_map_path,"rb"):NULL;
 if(!f)ok=0;
 if(ok)ok=inflate_assets(f,scene_images,scene_texture_count);
 if(f)fclose(f);
 int uploaded=0;
 for(int i=0;i<scene_texture_count;i++){
  struct image_asset *a=&scene_images[i];
  if(ok){
   glGenTextures(1,&scene_textures[i]);glBindTexture(GL_TEXTURE_2D,scene_textures[i]);
   int is_hud = (i >= first_hud_tex);
   if(a->format <= 2){
    uint32_t *rgba = malloc((size_t)a->width * a->height * sizeof(uint32_t));
    if(rgba){
     for(int p=0;p<a->width*a->height;p++){
      uint32_t alpha = a->data[p];
      rgba[p] = (alpha << 24) | 0x00FFFFFFu;
     }
     glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, a->width, a->height, 0, GL_RGBA, GL_UNSIGNED_BYTE, rgba);
     free(rgba);
    }
   } else {
    GLenum fmt=a->format==14?GL_COMPRESSED_RGBA_S3TC_DXT1_EXT:a->format==15?GL_COMPRESSED_RGBA_S3TC_DXT3_EXT:GL_COMPRESSED_RGBA_S3TC_DXT5_EXT;
    glCompressedTexImage2D(GL_TEXTURE_2D,0,fmt,a->width,a->height,0,a->size,a->data);
   }
   if(is_hud){
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
   } else {
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
   }
   GLenum err=glGetError();if(err){glDeleteTextures(1,&scene_textures[i]);scene_textures[i]=0;}else uploaded++;
   report("texture[%d] %dx%d fmt=%d hud=%d GL=%x\n",i,a->width,a->height,a->format,is_hud,err);
  }
  if(a->data)vglFree(a->data);a->data=NULL;
 }
 tex_hud_reticle = (idx_reticle >= 0) ? scene_textures[idx_reticle] : 0;
 tex_hud_weapon = (idx_weapon >= 0) ? scene_textures[idx_weapon] : 0;
 tex_hud_unit_bg = (idx_unit_bg >= 0) ? scene_textures[idx_unit_bg] : 0;
 tex_hud_weap_bg = (idx_weap_bg >= 0) ? scene_textures[idx_weap_bg] : 0;
 tex_hud_blip = (idx_blip >= 0) ? scene_textures[idx_blip] : 0;
 report("GPU textures uploaded=%d/%d (HUD reticle=%u weapon=%u unit_bg=%u)\n",uploaded,scene_texture_count,tex_hud_reticle,tex_hud_weapon,tex_hud_unit_bg);
 return ok;
}
static GLuint scene_vbo;
static int scene_vertices;
static int gpu_first_frame_logged;
static uint32_t disk_u32(const uint8_t *p){uint32_t v;memcpy(&v,p,4);return v;}

static inline void uncompress_halo_normal(uint32_t compressed, float *nx, float *ny, float *nz) {
    float val = (float)(int32_t)(compressed << 21);
    *nx = (val * (1.0f / 1048576.0f) + 1.0f) * (1.0f / 2047.0f);
    compressed >>= 11;
    val = (float)(int32_t)(compressed << 21);
    *ny = (val * (1.0f / 1048576.0f) + 1.0f) * (1.0f / 2047.0f);
    compressed >>= 11;
    val = (float)(int32_t)(compressed << 22);
    *nz = (val * (1.0f / 2097152.0f) + 1.0f) * (1.0f / 1023.0f);
}

static int gpu_scene_init(uint8_t *bsp,uint32_t bytes,uint32_t base){
 SceIoStat st;
 if(sceIoGetstat("ur0:data/libshacccg.suprx",&st)<0&&sceIoGetstat("ur0:data/external/libshacccg.suprx",&st)<0){
  report("GPU unavailable: libshacccg.suprx missing in ur0:data (or external).\n");return 0;
 }
 struct structure_bsp_header_disk *h=(void*)bsp;
 uint8_t *s=translate_bsp_pointer(bsp,bytes,base,h->base_address,0x110);
 if(!s)return 0;
 struct tag_block_disk *surfaces=(void*)(s+0xf8),*lightmaps=(void*)(s+0x104);
 if(surfaces->count<1||surfaces->count>131072||lightmaps->count<1||lightmaps->count>4096)return 0;
 uint16_t *indices=translate_bsp_pointer(bsp,bytes,base,surfaces->address,(uint32_t)surfaces->count*6);
 uint8_t *lm=translate_bsp_pointer(bsp,bytes,base,lightmaps->address,(uint32_t)lightmaps->count*32);
 if(!indices||!lm)return 0;
 /* vitaGL returns whether resolution fallback occurred, NOT init success. */
 GLboolean resolution_fallback=vglInitExtended(0,960,544,16*1024*1024,SCE_GXM_MULTISAMPLE_NONE);
 report("GPU init returned; resolution_fallback=%u\n",(unsigned)resolution_fallback);
 /* First scene retires vitaGL's splash thread before mesh allocation. */
 glClearColor(.025f,.03f,.04f,1);glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);vglSwapBuffers(GL_FALSE);
 report("GPU initial clear presented GL=%x\n",glGetError());
 struct gpu_vertex *mesh=vglMalloc((uint32_t)surfaces->count*3*sizeof(*mesh));
 if(!mesh)return 0;
 if(scene_vbo){glBindBuffer(GL_ARRAY_BUFFER,0);glDeleteBuffers(1,&scene_vbo);scene_vbo=0;}
 if(scene_texture_count>0){glDeleteTextures(scene_texture_count,scene_textures);scene_texture_count=0;}
 memset(scene_textures,0,sizeof(scene_textures));memset(scene_bitmap_ids,0,sizeof(scene_bitmap_ids));
 scene_vertices=0;scene_batch_count=0;int materials=0;
 for(int l=0;l<lightmaps->count;l++){
  uint32_t n=disk_u32(lm+l*32+20),ptr=disk_u32(lm+l*32+24);
  if(n>4096){vglFree(mesh);return 0;}
  uint8_t *m=translate_bsp_pointer(bsp,bytes,base,ptr,n*256);if(!m){vglFree(mesh);return 0;}
  for(uint32_t j=0;j<n;j++){
   uint8_t *mat=m+j*256;uint32_t first=disk_u32(mat+20),count=disk_u32(mat+24),vc=disk_u32(mat+180),sz=disk_u32(mat+236);
   uint8_t *v=translate_bsp_pointer(bsp,bytes,base,disk_u32(mat+248),sz);
   if(!v||vc>sz/32||first>(uint32_t)surfaces->count||count>(uint32_t)surfaces->count-first){vglFree(mesh);return 0;}
   if(scene_batch_count==512){vglFree(mesh);return 0;}
   struct gpu_batch *batch=&scene_batches[scene_batch_count++];
   batch->first=scene_vertices;batch->count=count*3;batch->texture=scene_texture_for_material(disk_u32(mat+12));
   float amb_r=*(float*)(mat+40),amb_g=*(float*)(mat+44),amb_b=*(float*)(mat+48);
   int32_t dist_cnt=*(int32_t*)(mat+52);
   float lt_r=*(float*)(mat+56),lt_g=*(float*)(mat+60),lt_b=*(float*)(mat+64);
   float dir_x=*(float*)(mat+68),dir_y=*(float*)(mat+72),dir_z=*(float*)(mat+76);
   if(dist_cnt<=0||(lt_r<=0.001f&&lt_g<=0.001f&&lt_b<=0.001f)){
    lt_r=0.85f;lt_g=0.85f;lt_b=0.85f;dir_x=-0.57735f;dir_y=-0.57735f;dir_z=-0.57735f;
   }
   (void)amb_g; (void)amb_b; (void)amb_r;

   for(uint32_t t=first;t<first+count;t++){
    if(scene_vertices+3>surfaces->count*3){vglFree(mesh);return 0;}
    float pos[3][3];
    for(int k=0;k<3;k++){uint16_t ix=indices[t*3+k];if(ix>=vc){vglFree(mesh);return 0;}memcpy(pos[k],v+ix*32,12);}
    float ux=pos[1][0]-pos[0][0],uy=pos[1][1]-pos[0][1],uz=pos[1][2]-pos[0][2];
    float vx=pos[2][0]-pos[0][0],vy=pos[2][1]-pos[0][1],vz=pos[2][2]-pos[0][2];
    float fn_x=uy*vz-uz*vy,fn_y=uz*vx-ux*vz,fn_z=ux*vy-uy*vx,fn_len=sqrtf(fn_x*fn_x+fn_y*fn_y+fn_z*fn_z);
    if(fn_len>1e-7f){fn_x/=fn_len;fn_y/=fn_len;fn_z/=fn_len;}
    for(int k=0;k<3;k++){
     uint16_t ix=indices[t*3+k];
     uint32_t comp_n = *(uint32_t*)(v+ix*32+12);
     float v_nx, v_ny, v_nz;
     uncompress_halo_normal(comp_n, &v_nx, &v_ny, &v_nz);
     float v_len=sqrtf(v_nx*v_nx+v_ny*v_ny+v_nz*v_nz);
     if(v_len>0.1f){v_nx/=v_len;v_ny/=v_len;v_nz/=v_len;}
     else if(fn_len>1e-7f){v_nx=fn_x;v_ny=fn_y;v_nz=fn_z;}
     else{v_nx=0.0f;v_ny=0.0f;v_nz=1.0f;}

     float dot=-(v_nx*dir_x+v_ny*dir_y+v_nz*dir_z);
     float diffuse=dot*0.5f+0.5f;
     float hemi=0.85f+0.15f*(v_nz>0.0f?v_nz:-v_nz*0.5f);
     float factor=0.68f+0.32f*(diffuse*0.70f+hemi*0.30f);
     if(factor>1.0f)factor=1.0f;
     if(factor<0.65f)factor=0.65f;
     uint32_t c=(uint32_t)(factor*255.0f);
     uint32_t color=0xff000000u|(c<<16)|(c<<8)|c;
     struct gpu_vertex *o=&mesh[scene_vertices++];
     o->x=pos[k][0];o->y=pos[k][1];o->z=pos[k][2];o->color=color;
     memcpy(&o->u,v+ix*32+24,8);
    }
   }
   materials++;
  }
 }
 glGenBuffers(1,&scene_vbo);glBindBuffer(GL_ARRAY_BUFFER,scene_vbo);
 glBufferData(GL_ARRAY_BUFFER,scene_vertices*sizeof(*mesh),mesh,GL_STATIC_DRAW);vglFree(mesh);
 glEnableClientState(GL_VERTEX_ARRAY);glEnableClientState(GL_COLOR_ARRAY);glEnableClientState(GL_TEXTURE_COORD_ARRAY);
 glVertexPointer(3,GL_FLOAT,sizeof(struct gpu_vertex),(void*)0);
 glColorPointer(4,GL_UNSIGNED_BYTE,sizeof(struct gpu_vertex),(void*)12);
 glTexCoordPointer(2,GL_FLOAT,sizeof(struct gpu_vertex),(void*)16);
 scene_upload_textures();
 glEnable(GL_DEPTH_TEST);glDisable(GL_CULL_FACE);
 glClearColor(.025f,.03f,.04f,1);
 if(log_file){fprintf(log_file,"GPU visual mesh materials=%d triangles=%d GL=%x\n",materials,scene_vertices/3,glGetError());fflush(log_file);}
 return scene_vertices>0;
}
struct hud_vertex {
    float x, y, z;
    uint32_t color;
};

static void draw_hud_quad(GLuint tex, float x, float y, float w, float h, uint32_t color) {
    if (!tex) return;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, tex);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glEnableClientState(GL_TEXTURE_COORD_ARRAY);

    float vx[4][3] = {
        {x, y, 0}, {x + w, y, 0}, {x + w, y + h, 0}, {x, y + h, 0}
    };
    float uv[4][2] = {
        {0, 0}, {1, 0}, {1, 1}, {0, 1}
    };
    uint32_t cols[4] = {color, color, color, color};

    glVertexPointer(3, GL_FLOAT, 0, vx);
    glColorPointer(4, GL_UNSIGNED_BYTE, 0, cols);
    glTexCoordPointer(2, GL_FLOAT, 0, uv);
    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
    glDisable(GL_TEXTURE_2D);
}

static void draw_weapon_mesh(const struct hud_vertex *verts, int count) {
    if (count <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(struct hud_vertex), &verts[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(struct hud_vertex), &verts[0].color);
    glDrawArrays(GL_TRIANGLES, 0, count);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_hud_lines(const struct hud_vertex *verts, int count) {
    if (count <= 0) return;
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glDisableClientState(GL_TEXTURE_COORD_ARRAY);
    glEnableClientState(GL_VERTEX_ARRAY);
    glEnableClientState(GL_COLOR_ARRAY);
    glVertexPointer(3, GL_FLOAT, sizeof(struct hud_vertex), &verts[0].x);
    glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(struct hud_vertex), &verts[0].color);
    glDrawArrays(GL_LINES, 0, count);
    glDisableClientState(GL_COLOR_ARRAY);
    glDisableClientState(GL_VERTEX_ARRAY);
}

static void draw_first_person_weapon(float pitch, float sway_x, float sway_y, int shooting) {
    (void)pitch; (void)sway_x; (void)sway_y;
    if (shooting) {
        glMatrixMode(GL_PROJECTION); glLoadIdentity();
        glFrustum(-.0364, .0364, -.020625, .020625, .05, 10);
        glMatrixMode(GL_MODELVIEW); glLoadIdentity();
        glTranslatef(0.12f, -0.08f, -0.30f);
        static const struct hud_vertex flash_verts[] = {
            {-0.05f, -0.05f, -0.2f, 0xE640D9FF}, {0.05f, -0.05f, -0.2f, 0xE640D9FF}, {0.0f, 0.08f, -0.2f, 0xE640D9FF},
            {0.0f, -0.06f, -0.2f, 0xFF80FFFF}, {0.0f, 0.09f, -0.2f, 0xFF80FFFF}, {0.0f, 0.02f, -0.3f, 0xFF80FFFF}
        };
        glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        draw_weapon_mesh(flash_verts, 6);
        glDisable(GL_BLEND);
    }
}

static void draw_master_chief_hud(float health, float shield, int ammo) {
    (void)ammo;
    glMatrixMode(GL_PROJECTION); glLoadIdentity();
    glOrtho(0, 960, 544, 0, -1, 1);
    glMatrixMode(GL_MODELVIEW); glLoadIdentity();
    glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_TEXTURE_2D);
    glDisable(GL_DEPTH_TEST);

    int cx = 480, cy = 272;
    /* Master Chief MA5B Assault Rifle Crosshair Reticle from authentic bitmap */
    if (tex_hud_reticle) {
        draw_hud_quad(tex_hud_reticle, (float)(cx - 36), (float)(cy - 36), 72.0f, 72.0f, 0xFFFFF200u);
    } else {
        int r = 18;
        struct hud_vertex reticle_lines[] = {
            {(float)(cx - r), (float)cy, 0, 0xFFFFF200u}, {(float)(cx - r/3), (float)cy, 0, 0xFFFFF200u},
            {(float)(cx + r/3), (float)cy, 0, 0xFFFFF200u}, {(float)(cx + r), (float)cy, 0, 0xFFFFF200u},
            {(float)cx, (float)(cy - r), 0, 0xFFFFF200u}, {(float)cx, (float)(cy - r/3), 0, 0xFFFFF200u},
            {(float)cx, (float)(cy + r/3), 0, 0xFFFFF200u}, {(float)cx, (float)(cy + r), 0, 0xFFFFF200u}
        };
        glLineWidth(2.0f);
        draw_hud_lines(reticle_lines, sizeof(reticle_lines)/sizeof(reticle_lines[0]));
        glLineWidth(1.0f);
    }

    /* Precision Aim Center Dot */
    struct hud_vertex dot[] = {
        {(float)(cx - 2), (float)(cy - 2), 0, 0xFFFFF200u}, {(float)(cx + 2), (float)(cy - 2), 0, 0xFFFFF200u}, {(float)cx, (float)(cy + 2), 0, 0xFFFFF200u}
    };
    draw_weapon_mesh(dot, 3);

    /* --- TOP RIGHT: ENERGY SHIELD & HEALTH MONITOR --- */
    int hud_x = 710, hud_y = 26;
    int hud_w = 210, hud_h = 16;

    if (tex_hud_unit_bg) {
        draw_hud_quad(tex_hud_unit_bg, (float)(hud_x - 14), (float)(hud_y - 8), (float)(hud_w + 28), (float)(hud_h + 34), 0xD0FFFFFF);
    } else {
        struct hud_vertex frame[] = {
            {(float)(hud_x - 12), (float)(hud_y - 6), 0, 0xCC1A1005}, {(float)(hud_x + hud_w + 6), (float)(hud_y - 6), 0, 0xCC1A1005}, {(float)(hud_x + hud_w + 6), (float)(hud_y + hud_h + 26), 0, 0xCC1A1005},
            {(float)(hud_x - 12), (float)(hud_y - 6), 0, 0xCC1A1005}, {(float)(hud_x + hud_w + 6), (float)(hud_y + hud_h + 26), 0, 0xCC1A1005}, {(float)(hud_x - 12), (float)(hud_y + hud_h + 26), 0, 0xCC1A1005}
        };
        draw_weapon_mesh(frame, 6);
    }

    /* 10-Segment Visor Energy Shield Bar - Authentic Halo Cyan */
    float s_ratio = shield > 1.0f ? 1.0f : (shield < 0.0f ? 0.0f : shield);
    int active_s_segs = (int)(10.0f * s_ratio);
    uint32_t s_col = (shield > 0.25f) ? 0xFFFFF200u : 0xFF0020FFu;
    struct hud_vertex shield_segs[10 * 6];
    int s_vcount = 0;
    int seg_sw = 19, seg_sgap = 2;
    for (int i = 0; i < 10; i++) {
        if (i < active_s_segs) {
            float sx = (float)(hud_x + i * (seg_sw + seg_sgap));
            shield_segs[s_vcount++] = (struct hud_vertex){sx, (float)hud_y, 0, s_col};
            shield_segs[s_vcount++] = (struct hud_vertex){sx + seg_sw, (float)hud_y, 0, s_col};
            shield_segs[s_vcount++] = (struct hud_vertex){sx + seg_sw, (float)(hud_y + hud_h), 0, s_col};
            shield_segs[s_vcount++] = (struct hud_vertex){sx, (float)hud_y, 0, s_col};
            shield_segs[s_vcount++] = (struct hud_vertex){sx + seg_sw, (float)(hud_y + hud_h), 0, s_col};
            shield_segs[s_vcount++] = (struct hud_vertex){sx, (float)(hud_y + hud_h), 0, s_col};
        }
    }
    if (s_vcount > 0) draw_weapon_mesh(shield_segs, s_vcount);

    /* Health Blocks */
    int health_y = hud_y + hud_h + 5;
    int seg_w = 23, seg_gap = 3;
    int seg_count = (int)(8.0f * (health > 1.0f ? 1.0f : (health < 0.0f ? 0.0f : health)));
    struct hud_vertex segs[8 * 6];
    int seg_vcount = 0;
    for (int i = 0; i < 8; i++) {
        float sx = (float)(hud_x + i * (seg_w + seg_gap));
        uint32_t col = (i < seg_count) ? ((health < 0.3f) ? 0xFF0020FFu : 0xFF20D8FFu) : 0x80302010u;
        segs[seg_vcount++] = (struct hud_vertex){sx, (float)health_y, 0, col};
        segs[seg_vcount++] = (struct hud_vertex){sx + seg_w, (float)health_y, 0, col};
        segs[seg_vcount++] = (struct hud_vertex){sx + seg_w, (float)(health_y + 11), 0, col};
        segs[seg_vcount++] = (struct hud_vertex){sx, (float)health_y, 0, col};
        segs[seg_vcount++] = (struct hud_vertex){sx + seg_w, (float)(health_y + 11), 0, col};
        segs[seg_vcount++] = (struct hud_vertex){sx, (float)(health_y + 11), 0, col};
    }
    draw_weapon_mesh(segs, seg_vcount);

    /* --- TOP RIGHT: WEAPON CARD AND AMMO --- */
    if (tex_hud_weap_bg) {
        draw_hud_quad(tex_hud_weap_bg, (float)(hud_x + 40), (float)(hud_y + 44), 170.0f, 44.0f, 0xD0FFFFFF);
    }
    if (tex_hud_weapon) {
        draw_hud_quad(tex_hud_weapon, (float)(hud_x + 50), (float)(hud_y + 48), 88.0f, 44.0f, 0xFFFFF200u);
    }

    /* --- BOTTOM LEFT: MOTION SENSOR RADAR DISK --- */
    float rx = 75.0f, ry = 465.0f, rr = 42.0f;
    struct hud_vertex radar_bg[] = {
        {rx - rr, ry - rr, 0, 0xB3332400}, {rx + rr, ry - rr, 0, 0xB3332400}, {rx + rr, ry + rr, 0, 0xB3332400},
        {rx - rr, ry - rr, 0, 0xB3332400}, {rx + rr, ry + rr, 0, 0xB3332400}, {rx - rr, ry + rr, 0, 0xB3332400}
    };
    draw_weapon_mesh(radar_bg, 6);

    struct hud_vertex radar_grid[] = {
        {rx - rr, ry, 0, 0x66FFEA00}, {rx + rr, ry, 0, 0x66FFEA00},
        {rx, ry - rr, 0, 0x66FFEA00}, {rx, ry + rr, 0, 0x66FFEA00}
    };
    draw_hud_lines(radar_grid, 4);

    /* Player Motion Center Indicator */
    struct hud_vertex pdot[] = {
        {rx - 3, ry - 3, 0, 0xFF33F2FF}, {rx + 3, ry - 3, 0, 0xFF33F2FF}, {rx, ry + 4, 0, 0xFF33F2FF}
    };
    draw_weapon_mesh(pdot, 3);

    /* Motion Contact Blips */
    if (tex_hud_blip) {
        draw_hud_quad(tex_hud_blip, rx + 16 - 7, ry - 14 - 7, 14, 14, 0xFF0000FFu);
        draw_hud_quad(tex_hud_blip, rx - 22 - 7, ry + 12 - 7, 14, 14, 0xFFFFF200u);
    } else {
        struct hud_vertex blips[] = {
            {rx + 16, ry - 14, 0, 0xFF0000FF}, {rx + 20, ry - 14, 0, 0xFF0000FF}, {rx + 18, ry - 10, 0, 0xFF0000FF},
            {rx - 22, ry + 12, 0, 0xFF00FFFF}, {rx - 18, ry + 12, 0, 0xFF00FFFF}, {rx - 20, ry + 16, 0, 0xFF00FFFF}
        };
        draw_weapon_mesh(blips, 6);
    }

    glEnable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
}

static void gpu_scene_draw_menu_bg(float yaw) {
    float sy = sinf(yaw), cy = cosf(yaw), pitch = 0.06f, sp = sinf(pitch), cp = cosf(pitch);
    float m[16] = { -sy, -cy * sp, -cy * cp, 0, cy, -sy * sp, -sy * cp, 0, 0, cp, -sp, 0, 0, 0, -2.5f, 1 };
    glClearColor(0.01f, 0.02f, 0.05f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glMatrixMode(GL_PROJECTION); glLoadIdentity(); glFrustum(-.0364, .0364, -.020625, .020625, .05, 80);
    glMatrixMode(GL_MODELVIEW); glLoadMatrixf(m);

    if (scene_vbo > 0 && scene_batch_count > 0) {
        glBindBuffer(GL_ARRAY_BUFFER, scene_vbo);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_COLOR_ARRAY);
        glEnableClientState(GL_TEXTURE_COORD_ARRAY);
        glVertexPointer(3, GL_FLOAT, sizeof(struct gpu_vertex), (void*)0);
        glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(struct gpu_vertex), (void*)12);
        glTexCoordPointer(2, GL_FLOAT, sizeof(struct gpu_vertex), (void*)16);

        for (int i = 0; i < scene_batch_count; i++) {
            struct gpu_batch *b = &scene_batches[i];
            if (b->texture >= 0 && scene_textures[b->texture]) {
                glEnable(GL_TEXTURE_2D);
                glBindTexture(GL_TEXTURE_2D, scene_textures[b->texture]);
            } else glDisable(GL_TEXTURE_2D);
            for (int n = 0; n < b->count; n += 30000) {
                int count = b->count - n; if (count > 30000) count = 30000;
                glDrawArrays(GL_TRIANGLES, b->first + n, count);
            }
        }
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}

static void gpu_scene_draw(float x,float y,float z,float yaw,float pitch, float sway_x, float sway_y, int shooting, float health, float shield, int ammo){
 float sy=sinf(yaw),cy=cosf(yaw),sp=sinf(pitch),cp=cosf(pitch);
 float m[16]={-sy,-cy*sp,-cy*cp,0,cy,-sy*sp,-sy*cp,0,0,cp,-sp,0,
              sy*x-cy*y,cy*sp*x+sy*sp*y-cp*z,cy*cp*x+sy*cp*y+sp*z,1};
 glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
 glMatrixMode(GL_PROJECTION);glLoadIdentity();glFrustum(-.0364,.0364,-.020625,.020625,.05,80);
 glMatrixMode(GL_MODELVIEW);glLoadMatrixf(m);

 glBindBuffer(GL_ARRAY_BUFFER, scene_vbo);
 glEnableClientState(GL_VERTEX_ARRAY);
 glEnableClientState(GL_COLOR_ARRAY);
 glEnableClientState(GL_TEXTURE_COORD_ARRAY);
 glVertexPointer(3, GL_FLOAT, sizeof(struct gpu_vertex), (void*)0);
 glColorPointer(4, GL_UNSIGNED_BYTE, sizeof(struct gpu_vertex), (void*)12);
 glTexCoordPointer(2, GL_FLOAT, sizeof(struct gpu_vertex), (void*)16);

 for(int i=0;i<scene_batch_count;i++){
  struct gpu_batch *b=&scene_batches[i];
  if(b->texture>=0&&scene_textures[b->texture]){glEnable(GL_TEXTURE_2D);glBindTexture(GL_TEXTURE_2D,scene_textures[b->texture]);}else glDisable(GL_TEXTURE_2D);
  for(int n=0;n<b->count;n+=30000){int count=b->count-n;if(count>30000)count=30000;glDrawArrays(GL_TRIANGLES,b->first+n,count);}
 }
 draw_first_person_weapon(pitch, sway_x, sway_y, shooting);
 draw_master_chief_hud(health, shield, ammo);
 vglSwapBuffers(GL_FALSE);
 if(!gpu_first_frame_logged&&log_file){fprintf(log_file,"GPU first mesh frame submitted vertices=%d GL=%x\n",scene_vertices,glGetError());fflush(log_file);gpu_first_frame_logged=1;}
}

static int run_first_person(uint8_t *bsp,uint32_t bsp_bytes,uint32_t xbox_base,
    float start_x,float start_y,float start_z,float start_facing){
    struct structure_bsp_header_disk *header=(struct structure_bsp_header_disk*)bsp;
    uint8_t *structure=translate_bsp_pointer(bsp,bsp_bytes,xbox_base,header->base_address,0xbc);
    struct tag_block_disk *collision=structure?(struct tag_block_disk*)(structure+0xb0):NULL;
    uint8_t *cb=(collision&&collision->count>0)?translate_bsp_pointer(bsp,bsp_bytes,xbox_base,collision->address,0x60):NULL;
    struct tag_block_disk *plane_block=cb?(struct tag_block_disk*)(cb+0x0c):NULL;
    struct tag_block_disk *surface_block=cb?(struct tag_block_disk*)(cb+0x3c):NULL;
    struct tag_block_disk *edge_block=cb?(struct tag_block_disk*)(cb+0x48):NULL;
    struct tag_block_disk *vertex_block=cb?(struct tag_block_disk*)(cb+0x54):NULL;
    struct plane3d_disk *planes=(plane_block&&plane_block->count>0&&plane_block->count<=131072)?translate_bsp_pointer(bsp,bsp_bytes,xbox_base,plane_block->address,(uint32_t)plane_block->count*sizeof(*planes)):NULL;
    struct collision_surface_disk *surfaces=(surface_block&&surface_block->count>0&&surface_block->count<=131072)?translate_bsp_pointer(bsp,bsp_bytes,xbox_base,surface_block->address,(uint32_t)surface_block->count*sizeof(*surfaces)):NULL;
    struct collision_edge_disk *edges=(edge_block&&edge_block->count>0&&edge_block->count<=262144)?translate_bsp_pointer(bsp,bsp_bytes,xbox_base,edge_block->address,(uint32_t)edge_block->count*sizeof(*edges)):NULL;
    struct collision_vertex_disk *vertices=(vertex_block&&vertex_block->count>0&&vertex_block->count<=131072)?translate_bsp_pointer(bsp,bsp_bytes,xbox_base,vertex_block->address,(uint32_t)vertex_block->count*sizeof(*vertices)):NULL;
    if(!planes||!surfaces||!edges||!vertices){report("first-person geometry pointers: FAIL\n");return 0;}

    struct collision_triangle *triangles=malloc((size_t)edge_block->count*sizeof(*triangles));int32_t triangle_count=0;
    if(!triangles){report("triangle cache allocation: FAIL\n");return 0;}
    for(int32_t si=0;si<surface_block->count;si++){
        int32_t polygon[8],edge=surfaces[si].first_edge,start=edge,count=0;int closed=0;
        while(count<8&&(uint32_t)edge<(uint32_t)edge_block->count){struct collision_edge_disk *e=&edges[edge];int32_t next;
            if(e->surface[0]==si){polygon[count++]=e->vertex[0];next=e->edge[0];}
            else if(e->surface[1]==si){polygon[count++]=e->vertex[1];next=e->edge[1];}
            else break;
            edge=next;if(edge==start){closed=1;break;}}
        if(!closed||count<3)continue;
        for(int j=1;j+1<count&&triangle_count<edge_block->count;j++){
            if((uint32_t)polygon[0]>=(uint32_t)vertex_block->count||(uint32_t)polygon[j]>=(uint32_t)vertex_block->count||(uint32_t)polygon[j+1]>=(uint32_t)vertex_block->count)continue;
            triangles[triangle_count].vertex[0]=(uint32_t)polygon[0];triangles[triangle_count].vertex[1]=(uint32_t)polygon[j];triangles[triangle_count].vertex[2]=(uint32_t)polygon[j+1];
            triangles[triangle_count].plane=(uint16_t)((uint32_t)surfaces[si].plane&0x7fffu);triangles[triangle_count].material=surfaces[si].material;triangle_count++;}
    }
    report("triangle cache surfaces=%ld triangles=%ld: %s\n",(long)surface_block->count,(long)triangle_count,triangle_count>0?"PASS":"FAIL");
    if(!triangle_count){free(triangles);return 0;}

    int16_t *screen_x=malloc((size_t)vertex_block->count*sizeof(*screen_x));
    int16_t *screen_y=malloc((size_t)vertex_block->count*sizeof(*screen_y));
    float *depth=malloc((size_t)vertex_block->count*sizeof(*depth));
    float *zbuffer=malloc(240u*136u*sizeof(*zbuffer));
    uint32_t *low_color=malloc(240u*136u*sizeof(*low_color));
    uint32_t *staging=malloc(960u*544u*sizeof(*staging));
    if(!screen_x||!screen_y||!depth||!zbuffer||!low_color||!staging){free(triangles);free(screen_x);free(screen_y);free(depth);free(zbuffer);free(low_color);free(staging);report("first-person buffers: FAIL\n");return 0;}
    SceDisplayFrameBuf fb={0};fb.size=sizeof(fb);
    if(sceDisplayGetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME)<0||!fb.base){free(triangles);free(screen_x);free(screen_y);free(depth);free(zbuffer);free(low_color);free(staging);return 0;}
    if(!prepare_ground(triangles,triangle_count,vertices)){
        free(triangles);free(screen_x);free(screen_y);free(depth);free(zbuffer);free(low_color);free(staging);return 0;
    }
    int gpu_ready=gpu_scene_init(bsp,bsp_bytes,xbox_base);
    if(!gpu_ready){sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME);report("GPU scene unavailable; using software fallback.\n");}
    int analog_rc=sceCtrlSetSamplingMode(SCE_CTRL_MODE_ANALOG);
    float ground=start_z;if(!find_ground(triangles,triangle_count,vertices,start_x,start_y,start_z+0.5f,&ground))ground=start_z;
    float camera[3]={start_x,start_y,ground+0.65f},foot_z=ground,vertical_velocity=0,yaw=start_facing,pitch=0.0f;
    float health = 1.0f, shield = 1.0f, recharge_delay = 0.0f;
    uint32_t *frame=fb.base,previous=0;int redraw=1,running=1,solid=1,noclip=0,grounded=1,render_ms=0,physics_ms=0;SceUInt64 last=sceKernelGetProcessTimeWide();
    report("WALK start=(%.3f %.3f %.3f) ground=%.3f facing=%.3f analog=%08lx\n",(double)camera[0],(double)camera[1],(double)camera[2],(double)ground,(double)yaw,(unsigned long)analog_rc);
    while(running){
        SceUInt64 now=sceKernelGetProcessTimeWide();float dt=(float)(now-last)/1000000.0f;last=now;if(dt<0.001f)dt=0.001f;if(dt>0.05f)dt=0.05f;
        SceCtrlData pad={0};sceCtrlPeekBufferPositive(0,&pad,1);uint32_t pressed=pad.buttons&~previous;previous=pad.buttons;
        if((pad.buttons&(SCE_CTRL_SELECT|SCE_CTRL_START))==(SCE_CTRL_SELECT|SCE_CTRL_START)){running=0;break;}
        if(pressed&SCE_CTRL_CIRCLE){running=0;break;}
        float lx=((int)pad.lx-128)/127.0f,ly=((int)pad.ly-128)/127.0f,rx=((int)pad.rx-128)/127.0f,ry=((int)pad.ry-128)/127.0f;
        if(fabsf(lx)<0.14f)lx=0;
        if(fabsf(ly)<0.14f)ly=0;
        if(fabsf(rx)<0.14f)rx=0;
        if(fabsf(ry)<0.14f)ry=0;
        if(rx||ry){yaw+=rx*2.2f*dt;pitch-=ry*1.7f*dt;if(pitch>1.35f)pitch=1.35f;if(pitch<-1.35f)pitch=-1.35f;redraw=1;}
        if(pressed&SCE_CTRL_SQUARE){noclip=!noclip;vertical_velocity=0;redraw=1;report("movement mode=%s\n",noclip?"NOCLIP":"WALK");}
        if(pressed&SCE_CTRL_TRIANGLE){solid=!solid;redraw=1;}
        if(pressed&SCE_CTRL_DOWN){camera[0]=start_x;camera[1]=start_y;foot_z=ground;camera[2]=foot_z+0.65f;vertical_velocity=0;yaw=start_facing;pitch=0;grounded=1;redraw=1;}
        SceUInt64 physics_begin=sceKernelGetProcessTimeWide();
        float magnitude=sqrtf(lx*lx+ly*ly);if(magnitude>1){lx/=magnitude;ly/=magnitude;}
        float forward=-ly,strafe=lx,speed=(pad.buttons&SCE_CTRL_RTRIGGER)?5.0f:2.2f;
        if(forward||strafe){float cy=cosf(yaw),sy=sinf(yaw),nx=camera[0]+(cy*forward-sy*strafe)*speed*dt,ny=camera[1]+(sy*forward+cy*strafe)*speed*dt;
            if(noclip){camera[0]=nx;camera[1]=ny;}else{
                float dx=nx-camera[0],dy=ny-camera[1];
                if(!body_blocked(triangles,triangle_count,vertices,camera[0],camera[1],foot_z,dx,dy)){camera[0]=nx;camera[1]=ny;}
                else {
                    if(!body_blocked(triangles,triangle_count,vertices,camera[0],camera[1],foot_z,dx,0))camera[0]=nx;
                    if(!body_blocked(triangles,triangle_count,vertices,camera[0],camera[1],foot_z,0,dy))camera[1]=ny;
                }
            }
            redraw=1;}
        if(noclip){if(pad.buttons&SCE_CTRL_CROSS){camera[2]+=2.0f*dt;redraw=1;}if(pad.buttons&SCE_CTRL_LTRIGGER){camera[2]-=2.0f*dt;redraw=1;}foot_z=camera[2]-0.65f;}
        else{
            float current_ground;int has_ground=find_ground(triangles,triangle_count,vertices,camera[0],camera[1],foot_z+0.30f,&current_ground);
            grounded=has_ground&&foot_z<=current_ground+0.035f&&vertical_velocity<=0;
            if((pressed&SCE_CTRL_CROSS)&&grounded){vertical_velocity=3.4f;grounded=0;}
            if(!grounded)vertical_velocity-=9.2f*dt;
            if(vertical_velocity>0&&segment_blocked(triangles,triangle_count,vertices,camera[0],camera[1],foot_z+.75f,0,0,vertical_velocity*dt+.02f))vertical_velocity=0;
            foot_z+=vertical_velocity*dt;
            if (has_ground && foot_z <= current_ground) {
                if (vertical_velocity < -7.0f) {
                    float dmg = (-vertical_velocity - 7.0f) * 0.25f;
                    if (shield > 0.0f) { shield -= dmg; if (shield < 0.0f) { health += shield; shield = 0.0f; } }
                    else { health -= dmg; if (health < 0.0f) health = 0.0f; }
                    recharge_delay = 3.5f;
                }
                foot_z = current_ground; vertical_velocity = 0; grounded = 1;
            }
            if (recharge_delay > 0.0f) { recharge_delay -= dt; }
            else if (shield < 1.0f) { shield += dt * 0.45f; if (shield > 1.0f) shield = 1.0f; }
            camera[2]=foot_z+0.65f;if(!grounded||vertical_velocity!=0)redraw=1;
        }
        physics_ms=(int)((sceKernelGetProcessTimeWide()-physics_begin)/1000u);
        if(gpu_ready&&redraw){
            SceUInt64 gpu_begin=sceKernelGetProcessTimeWide();
            glPolygonMode(GL_FRONT_AND_BACK,solid?GL_FILL:GL_LINE);
            static float walk_timer = 0.0f;
            if (forward != 0 || strafe != 0) walk_timer += dt * 10.0f;
            float sway_x = sinf(walk_timer) * 0.4f;
            float sway_y = cosf(walk_timer * 2.0f) * 0.4f;
            int shooting = (pad.buttons & (SCE_CTRL_RTRIGGER | SCE_CTRL_CROSS)) ? 1 : 0;
            gpu_scene_draw(camera[0],camera[1],camera[2],yaw,pitch, sway_x, sway_y, shooting, health, shield, 60);
            render_ms=(int)((sceKernelGetProcessTimeWide()-gpu_begin)/1000u);redraw=0;
        }
        if(redraw){
            SceUInt64 render_begin=sceKernelGetProcessTimeWide();
            for(int y=0;y<136;y++)for(int x=0;x<240;x++){uint32_t blue=7u+(uint32_t)(y*18/136);low_color[y*240+x]=0xff000000u|(blue<<16)|(2u<<8);}
            float cy=cosf(yaw),sy=sinf(yaw),cp=cosf(pitch),sp=sinf(pitch),focal=165.0f;
            for(int32_t i=0;i<vertex_block->count;i++){
                float dx=vertices[i].x-camera[0],dy=vertices[i].y-camera[1],dz=vertices[i].z-camera[2];float right=-dx*sy+dy*cy,forward_axis=dx*cy+dy*sy,view_z=forward_axis*cp+dz*sp,view_y=dz*cp-forward_axis*sp;depth[i]=view_z;
                if(view_z>0.08f){float sx=120.0f+right*focal/view_z,syy=68.0f-view_y*focal/view_z;if(sx<-2048)sx=-2048;if(sx>2048)sx=2048;if(syy<-2048)syy=-2048;if(syy>2048)syy=2048;screen_x[i]=(int16_t)sx;screen_y[i]=(int16_t)syy;}else{screen_x[i]=-32768;screen_y[i]=-32768;}
            }
            int drawn=0;
            if(solid){
                for(int i=0;i<240*136;i++)zbuffer[i]=0.0f;
                float view_dx=cy*cp,view_dy=sy*cp,view_dz=sp;
                for(int32_t ti=0;ti<triangle_count;ti++){uint32_t a=triangles[ti].vertex[0],b=triangles[ti].vertex[1],c=triangles[ti].vertex[2];
                    if(depth[a]<=0.08f||depth[b]<=0.08f||depth[c]<=0.08f||depth[a]>50.0f||depth[b]>50.0f||depth[c]>50.0f)continue;
                    int x0=screen_x[a],x1=screen_x[b],x2=screen_x[c],y0=screen_y[a],y1=screen_y[b],y2=screen_y[c];if((x0<0&&x1<0&&x2<0)||(x0>239&&x1>239&&x2>239)||(y0<0&&y1<0&&y2<0)||(y0>135&&y1>135&&y2>135))continue;
                    float facing=.5f;if(triangles[ti].plane<(uint32_t)plane_block->count){struct plane3d_disk *p=&planes[triangles[ti].plane];facing=fabsf(p->x*view_dx+p->y*view_dy+p->z*view_dz);}
                    uint32_t shade=50u+(uint32_t)(180.0f*facing);if(shade>230u)shade=230u;uint32_t tint=(uint32_t)(triangles[ti].material*37)&63u;uint32_t color=0xff000000u|((shade+tint>255?255:shade+tint)<<16)|(shade<<8)|(30u+tint/2u);
                    fill_triangle_fast(low_color,zbuffer,x0,y0,depth[a],x1,y1,depth[b],x2,y2,depth[c],color);drawn++;}
            }else{
                int step=edge_block->count>16000?(edge_block->count+15999)/16000:1;for(int32_t i=0;i<edge_block->count;i+=step){int32_t a=edges[i].vertex[0],b=edges[i].vertex[1];if((uint32_t)a>=(uint32_t)vertex_block->count||(uint32_t)b>=(uint32_t)vertex_block->count||depth[a]<=0.08f||depth[b]<=0.08f)continue;draw_line(low_color,240,screen_x[a],screen_y[a],screen_x[b],screen_y[b],0xfff0d030u);drawn++;}}
            for(int y=0;y<136;y++)for(int yy=0;yy<4;yy++){uint32_t *dst=staging+(y*4+yy)*960;for(int x=0;x<240;x++){uint32_t c=low_color[y*240+x];dst[x*4]=c;dst[x*4+1]=c;dst[x*4+2]=c;dst[x*4+3]=c;}}
            draw_line(staging,960,472,272,488,272,0xffffffffu);draw_line(staging,960,480,264,480,280,0xffffffffu);
            for(int y=0;y<544;y++)memcpy(frame+y*(int)fb.pitch,staging+y*960,960*sizeof(uint32_t));
            render_ms=(int)((sceKernelGetProcessTimeWide()-render_begin)/1000u);
            int xy[2]={1,1};psvDebugScreenSetCoordsXY(&xy[0],&xy[1]);psvDebugScreenPrintf("A10 WALK - %s - %d tris - render %d ms / physics %d ms\n",solid?"SOLIDO":"ALAMBRE",drawn,render_ms,physics_ms);psvDebugScreenPrintf("L mover | R mirar | X saltar | R correr\n");psvDebugScreenPrintf("Cuadrado %s | Triangulo modo | Abajo reaparecer\n",noclip?"CAMINAR":"NOCLIP");redraw=0;
        }
        sceDisplayWaitVblankStart();
    }
    if(gpu_ready){glFinish();glDeleteTextures(scene_texture_count,scene_textures);glDeleteBuffers(1,&scene_vbo);sceDisplaySetFrameBuf(&fb,SCE_DISPLAY_SETBUF_NEXTFRAME);sceDisplayWaitVblankStart();}
    report("WALK end=(%.3f %.3f %.3f) yaw=%.3f mode=%s last_render_ms=%d\n",(double)camera[0],(double)camera[1],(double)camera[2],(double)yaw,noclip?"NOCLIP":"WALK",render_ms);
    free(ground_faces);ground_faces=NULL;ground_count=0;
    free(triangles);free(screen_x);free(screen_y);free(depth);free(zbuffer);free(low_color);free(staging);return 1;
}
static int load_selected_map(uint8_t *tags, int mode) {
    const char *a10_paths[]={
        "ux0:data/halo/maps/a10.map","ux0:data/halo/maps_es/a10.map",
        "ux0:data/halo-vita/maps/a10.map","ux0:data/halo-vita/maps_es/a10.map"
    };
    const char *mp_paths[]={
        "ux0:data/halo/maps/bloodgulch.map","ux0:data/halo/maps_es/bloodgulch.map",
        "ux0:data/halo-vita/maps/bloodgulch.map","ux0:data/halo-vita/maps_es/bloodgulch.map",
        "ux0:data/halo/maps/beavercreek.map","ux0:data/halo/maps_es/beavercreek.map"
    };
    const char **paths = (mode == 3) ? mp_paths : a10_paths;
    unsigned path_count = (mode == 3) ? (sizeof(mp_paths)/sizeof(mp_paths[0])) : (sizeof(a10_paths)/sizeof(a10_paths[0]));
    FILE *f=NULL;const char *path=NULL;struct cache_file_header mh;
    for(unsigned i=0;i<path_count;i++)if((f=fopen(paths[i],"rb"))){path=paths[i];break;}
    if(!f){report("MAP_LOAD=FAIL map missing\n");return 0;}
    int ok=read_header(f,&mh)&&mh.header_signature==HEAD_SIGNATURE&&mh.footer_signature==FOOT_SIGNATURE&&mh.version==5&&mh.tag_data_size>0&&mh.tag_data_size<=0x01600000;
    report("map path=%s logical=%ld tags_offset=%08lx tags_bytes=%ld header=%s\n",path,(long)mh.file_length,(unsigned long)(uint32_t)mh.tag_data_offset,(long)mh.tag_data_size,ok?"PASS":"FAIL");
    uint8_t *bsp=physical_memory_get_game_state_base_address();struct campaign_cache_header cache={0};int cache_hit=0;
    if(ok){memset(tags,0xcd,0x01600000);memset(bsp,0xcd,GAME_STATE_BYTES);cache_hit=load_campaign_cache(&mh,tags,bsp,&cache);if(!cache_hit)ok=inflate_campaign_data(f,&mh,tags,bsp);}
    struct cache_file_tag_header *th=(struct cache_file_tag_header*)tags;
    if(ok)ok=th->signature==TAGS_SIGNATURE&&th->tag_count>0&&th->tag_count<65536;
    report("a10 tag header signature=%08lx count=%ld scenario=%08lx: %s\n",(unsigned long)th->signature,(long)th->tag_count,(unsigned long)(uint32_t)th->scenario_tag_index,ok?"PASS":"FAIL");
    struct cache_file_tag_instance *instances=ok?translate_tag_pointer(tags,mh.tag_data_size,th->tag_instances,th->tag_count*sizeof(*instances)):NULL;
    uint32_t names=0,datums=0,sbsp_tags=0;
    if(!instances)ok=0;
    for(int32_t i=0;ok&&i<th->tag_count;i++){
        const char *name=translate_tag_pointer(tags,mh.tag_data_size,instances[i].name,1);
        if(name&&memchr(name,0,mh.tag_data_size-(instances[i].name-XBOX_TAG_BASE)))names++;
        if((instances[i].tag_index&0xffffu)==(uint32_t)i)datums++;
        if(instances[i].group_tag==0x73627370u)sbsp_tags++;
    }
    int slot=(int)((uint32_t)th->scenario_tag_index&0xffffu);
    struct cache_file_tag_instance *scenario=(ok&&slot>=0&&slot<th->tag_count)?&instances[slot]:NULL;
    const char *scenario_name=scenario?translate_tag_pointer(tags,mh.tag_data_size,scenario->name,1):NULL;
    ok=ok&&names==(uint32_t)th->tag_count&&datums==(uint32_t)th->tag_count&&scenario&&scenario->group_tag==0x73636e72u&&scenario_name!=NULL;
    report("a10 table names=%lu/%ld datums=%lu/%ld sbsp_tags=%lu scenario=%s: %s\n",(unsigned long)names,(long)th->tag_count,(unsigned long)datums,(long)th->tag_count,(unsigned long)sbsp_tags,scenario_name?scenario_name:"<invalid>",ok?"PASS":"FAIL");
    uint8_t *scenario_data=ok?translate_tag_pointer(tags,mh.tag_data_size,scenario->base_address,0x5b0):NULL;
    float start_x=0,start_y=0,start_z=0,start_facing=0;
    struct tag_block_disk *players=scenario_data?(struct tag_block_disk*)(scenario_data+0x354):NULL;
    struct player_start_disk *player=(players&&players->count>0)?translate_tag_pointer(tags,mh.tag_data_size,players->address,sizeof(*player)):NULL;
    if(player){start_x=player->x;start_y=player->y;start_z=player->z;start_facing=player->facing;report("player spawn count=%ld position=(%.3f %.3f %.3f) facing=%.3f bsp=%d: PASS\n",(long)players->count,(double)start_x,(double)start_y,(double)start_z,(double)start_facing,player->bsp);}
    else {report("player spawn: FAIL\n");ok=0;}
    struct tag_block_disk *block=scenario_data?(struct tag_block_disk*)(scenario_data+0x5a4):NULL;
    struct scenario_bsp_reference_disk *refs=(block&&block->count>0&&block->count<64)?translate_tag_pointer(tags,mh.tag_data_size,block->address,block->count*sizeof(*refs)):NULL;
    if(!refs)ok=0;
    report("scenario BSP references=%ld: %s\n",block?(long)block->count:-1L,refs?"PASS":"FAIL");
    for(int32_t i=0;refs&&i<block->count;i++){
        const char *name=translate_tag_pointer(tags,mh.tag_data_size,refs[i].name,1);
        int valid=refs[i].group_tag==0x73627370u&&refs[i].file_offset>=0&&refs[i].file_size>0&&refs[i].file_offset<=mh.file_length-refs[i].file_size;
        report(" bsp[%ld] offset=%08lx bytes=%ld xbox_base=%08lx datum=%08lx name=%s %s\n",(long)i,(unsigned long)(uint32_t)refs[i].file_offset,(long)refs[i].file_size,(unsigned long)refs[i].base_address,(unsigned long)(uint32_t)refs[i].tag_index,name?name:"<invalid>",valid?"PASS":"FAIL");ok&=valid;
    }
    uint32_t bsp_bytes=0,bsp_base=0;
    if(ok&&block->count>0&&refs[0].file_size>0&&(uint32_t)refs[0].file_size<=GAME_STATE_BYTES){
        bsp_bytes=(uint32_t)refs[0].file_size;bsp_base=refs[0].base_address;
        if(!cache_hit){
            if((uint32_t)refs[0].file_offset != CACHE_HEADER_SIZE && (uint32_t)refs[0].file_offset >= CACHE_HEADER_SIZE){
                memmove(bsp, bsp + (refs[0].file_offset - CACHE_HEADER_SIZE), bsp_bytes);
            }
            save_campaign_cache(&mh, tags, bsp, (uint32_t)refs[0].file_offset, bsp_bytes, bsp_base);
        }
        struct structure_bsp_header_disk *bh=(struct structure_bsp_header_disk*)bsp;
        int header_ok=ok&&bh->signature==0x73627370u&&bh->vertex_count>0&&bh->index_count>0;
        report("first BSP signature=%08lx vertices=%ld indices=%ld vertex_ptr=%08lx index_ptr=%08lx: %s\n",(unsigned long)bh->signature,(long)bh->vertex_count,(long)bh->index_count,(unsigned long)bh->vertex_buffers,(unsigned long)bh->index_buffers,header_ok?"PASS":"FAIL");ok=header_ok;
    }
    fclose(f);scene_tags=tags;scene_tag_bytes=mh.tag_data_size;scene_map_path=path;report("MAP_LOAD=%s\n",ok?"PASS":"FAIL");if(ok)ok=run_first_person(bsp,bsp_bytes,bsp_base,start_x,start_y,start_z,start_facing);return ok;
}

static int validate_tag_table(uint8_t *tags, uint32_t tag_bytes) {
    struct cache_file_tag_header *header = (struct cache_file_tag_header *)tags;
    struct cache_file_tag_instance *instances = translate_tag_pointer(tags, tag_bytes,
        header->tag_instances, (uint32_t)header->tag_count * sizeof(*instances));
    struct { uint32_t tag; uint32_t count; } groups[64];
    uint32_t group_count = 0;
    uint32_t valid_names = 0, valid_data = 0, valid_datums = 0;
    int scenario_slot = (int)((uint32_t)header->scenario_tag_index & 0xFFFFu);

    if (!instances || header->tag_count <= 0 || header->tag_count > 65535) return 0;
    for (int32_t i = 0; i < header->tag_count; ++i) {
        const struct cache_file_tag_instance *instance = &instances[i];
        const char *name = translate_tag_pointer(tags, tag_bytes, instance->name, 1);
        if (name) {
            uint32_t left = tag_bytes - (instance->name - XBOX_TAG_BASE);
            if (memchr(name, 0, left)) ++valid_names;
        }
        if (instance->base_address == 0 || translate_tag_pointer(tags, tag_bytes, instance->base_address, 1))
            ++valid_data;
        if ((instance->tag_index & 0xFFFFu) == (uint32_t)i) ++valid_datums;

        uint32_t g;
        for (g = 0; g < group_count && groups[g].tag != instance->group_tag; ++g) {}
        if (g == group_count && group_count < sizeof(groups) / sizeof(groups[0])) {
            groups[group_count].tag = instance->group_tag;
            groups[group_count].count = 0;
            ++group_count;
        }
        if (g < group_count) ++groups[g].count;
    }

    report("pointer rebase xbox=%08lx vita=%p delta=%08lx\n", (unsigned long)XBOX_TAG_BASE,
        tags, (unsigned long)((uintptr_t)tags - XBOX_TAG_BASE));
    report("instances=%p names=%lu/%ld data=%lu/%ld datums=%lu/%ld groups=%lu\n",
        instances, (unsigned long)valid_names, (long)header->tag_count,
        (unsigned long)valid_data, (long)header->tag_count,
        (unsigned long)valid_datums, (long)header->tag_count, (unsigned long)group_count);

    if (scenario_slot < 0 || scenario_slot >= header->tag_count) return 0;
    const struct cache_file_tag_instance *scenario = &instances[scenario_slot];
    const char *scenario_name = translate_tag_pointer(tags, tag_bytes, scenario->name, 1);
    char scenario_group[5];
    group_name(scenario->group_tag, scenario_group);
    report("scenario slot=%d datum=%08lx group=%s name=%s\n", scenario_slot,
        (unsigned long)scenario->tag_index, scenario_group, scenario_name ? scenario_name : "<invalid>");
    report("group inventory:");
    for (uint32_t g = 0; g < group_count; ++g) {
        char text[5];
        group_name(groups[g].tag, text);
        report(" %s=%lu", text, (unsigned long)groups[g].count);
        if ((g + 1) % 5 == 0 && g + 1 < group_count) report("\n                ");
    }
    report("\n");

    return valid_names == (uint32_t)header->tag_count &&
        valid_data == (uint32_t)header->tag_count &&
        valid_datums == (uint32_t)header->tag_count &&
        scenario->tag_index == (uint32_t)header->scenario_tag_index &&
        scenario->group_tag == 0x73636E72u && scenario_name && strcmp(scenario_name, "levels\\ui\\ui") == 0;
}

int main(void) {
    const char *paths[] = {
        "ux0:data/halo/maps/ui.map", "ux0:data/halo/maps_es/ui.map",
        "ux0:data/halo-vita/maps/ui.map", "ux0:data/halo-vita/maps_es/ui.map"
    };
    const char *opened_path = NULL;
    struct cache_file_header header;
    FILE *map = NULL;
    int passed = 1;

    sceIoMkdir("ux0:data/halo-vita-diagnostic", 0777);
    log_file = fopen("ux0:data/halo-vita-diagnostic/textures-20.txt", "w");
    if (psvDebugScreenInit() < 0) sceKernelExitProcess(1);
    report("HALO VITA GPU TEXTURES & LIGHTING TEST 2.0\n");
    report("GPU visual BSP mesh, repeating textures, Half-Lambert lighting & native HUD.\n\n");

    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); ++i) {
        map = fopen(paths[i], "rb");
        if (map) { opened_path = paths[i]; break; }
    }
    if (!map) {
        report("MAP_TEST=FAIL ui.map not found\n");
        report("Expected: ux0:data/halo/maps/ui.map\n");
        passed = 0;
    } else if (!read_header(map, &header)) {
        report("MAP_TEST=FAIL cannot read 0x800-byte header\n");
        passed = 0;
    } else {
        header.name[sizeof(header.name) - 1] = 0;
        header.build[sizeof(header.build) - 1] = 0;
        report("map=%s\nname=%s build=%s version=%ld\n", opened_path, header.name,
            header.build, (long)header.version);
        report("logical_bytes=%ld tags_offset=%08lx tags_bytes=%ld\n", (long)header.file_length,
            (unsigned long)(uint32_t)header.tag_data_offset, (long)header.tag_data_size);
        passed = header.header_signature == HEAD_SIGNATURE && header.footer_signature == FOOT_SIGNATURE &&
            header.version == 5 && strcmp(header.build, "01.01.14.2342") == 0 &&
            header.tag_data_offset >= (int32_t)CACHE_HEADER_SIZE && header.tag_data_size >= 0x24 &&
            header.tag_data_size <= 0x01600000;
        report("cache header: %s\n", passed ? "PASS" : "FAIL");

        if (passed) {
            int rc = halo_vita_memory_init();
            report("arena init=%08lx base=%p\n", (unsigned long)rc, halo_vita_address(0));
            if (rc < 0) passed = 0;
            else {
                physical_memory_allocate();
                uint8_t *tags = physical_memory_get_tag_cache_base_address();
                memset(tags, 0xCD, 0x01600000);
                passed = inflate_tag_data(map, &header, tags);
                report("tag data inflate: %s\n", passed ? "PASS" : "FAIL");
                if (passed) {
                    struct cache_file_tag_header *tag_header = (struct cache_file_tag_header *)tags;
                    report("tag_header signature=%08lx count=%ld scenario=%ld\n",
                        (unsigned long)tag_header->signature, (long)tag_header->tag_count,
                        (long)tag_header->scenario_tag_index);
                    passed = tag_header->signature == TAGS_SIGNATURE && tag_header->tag_count > 0 &&
                        tag_header->tag_count < 65536 && (uint32_t)tag_header->scenario_tag_index != 0xFFFFFFFFu;
                    report("real tag table: %s\n", passed ? "PASS" : "FAIL");
                    if (passed) {
                        passed = validate_tag_table(tags, (uint32_t)header.tag_data_size);
                        report("tag pointer walk: %s\n", passed ? "PASS" : "FAIL");
                        if (passed) {
                            int ui_result = run_ui_08(map, tags, (uint32_t)header.tag_data_size);
                            report("interactive menu result=%d\n", ui_result);
                            passed = ui_result > 0;
                            if (ui_result == 2 || ui_result == 3) passed = load_selected_map(tags, ui_result);
                        }
                    }
                }
                physical_memory_free();
                halo_vita_memory_dispose();
            }
        }
    }
    if (map) fclose(map);
    report("\nWALK_TEST=%s\n", passed ? "PASS" : "FAIL");
    report("SELECT + START: salir\n");
    report("Informe: ux0:data/halo-vita-diagnostic/textures-20.txt\n");
    if (log_error) psvDebugScreenPrintf("ERROR al escribir el informe\n");

    SceCtrlData pad = {0};
    do {
        sceCtrlPeekBufferPositive(0, &pad, 1);
        sceDisplayWaitVblankStart();
    } while ((pad.buttons & (SCE_CTRL_SELECT | SCE_CTRL_START)) != (SCE_CTRL_SELECT | SCE_CTRL_START));
    if (log_file) fclose(log_file);
    sceKernelExitProcess(passed ? 0 : 1);
    return 0;
}



