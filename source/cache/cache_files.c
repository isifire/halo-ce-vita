/*
CACHE_FILES.C

symbols in this file:
001A9180 00f0:
	_cache_files_map_directory (0000)
001A9270 0030:
	_scenario_tags_unload (0000)
001A92A0 0010:
	_tag_files_open (0000)
001A92B0 0010:
	_tag_files_close (0000)
001A92C0 0040:
	_tag_groups_checksum (0000)
001A9300 0010:
	_cache_files_get_checksum (0000)
001A9310 00b0:
	_tag_loaded (0000)
001A93C0 0020:
	_cache_files_enable_writes (0000)
001A93E0 0090:
	_cache_files_disable_writes (0000)
001A9470 0020:
	_tag_block_resize (0000)
001A9490 0020:
	_tag_data_resize (0000)
001A94B0 0020:
	_tag_block_add_element (0000)
001A94D0 0010:
	_tag_block_delete_element (0000)
001A94E0 0020:
	_tag_load (0000)
001A9500 0010:
	_tag_unload (0000)
001A9510 0020:
	_tag_file_get_path (0000)
001A9530 0010:
	_tag_reference_set (0000)
001A9540 0020:
	_tag_iterator_new (0000)
001A9560 0070:
	_tag_iterator_next (0000)
001A95D0 00f0:
	_cache_get_tag_instance (0000)
001A96C0 0100:
	_cache_file_header_verify (0000)
001A97C0 0090:
	_cache_files_give_time_to_precache (0000)
001A9850 0130:
	_scenario_tags_load (0000)
001A9980 0120:
	_scenario_structure_bsp_load (0000)
001A9AA0 0080:
	_scenario_structure_bsp_unload (0000)
001A9B20 00b0:
	_tag_get (0000)
001A9BD0 0020:
	_tag_get_name (0000)
001A9BF0 0020:
	_tag_get_group_tag (0000)
002A62D8 0009:
	??_C@_08NDLPNBDL@d?3?2maps?2?$AA@ (0000)
002A62E4 000c:
	??_C@_0M@KPLLEAGM@d?3?2maps_it?2?$AA@ (0000)
002A62F0 000c:
	??_C@_0M@OACCOJFB@d?3?2maps_es?2?$AA@ (0000)
002A62FC 000c:
	??_C@_0M@PDFFCMII@d?3?2maps_fr?2?$AA@ (0000)
002A6308 000c:
	??_C@_0M@EADFFBPG@d?3?2maps_de?2?$AA@ (0000)
002A6314 001e:
	??_C@_0BO@JHMCCGLN@no?5valid?5map?5directory?5exists?$AA@ (0000)
002A6334 0023:
	??_C@_0CD@DIDKODIK@c?3?2halo?2SOURCE?2cache?2cache_files@ (0000)
002A6358 001f:
	??_C@_0BP@GJKOKCFG@cache_file_globals?4tags_loaded?$AA@ (0000)
002A6378 0015:
	??_C@_0BF@KAIMLJJI@global_tag_instances?$AA@ (0000)
002A6390 003d:
	??_C@_0DN@EPHHKDMF@tag_block_resize?$CI?$CJ?5is?5not?5suppor@ (0000)
002A63D0 003c:
	??_C@_0DM@FNPDMPEA@tag_data_resize?$CI?$CJ?5is?5not?5support@ (0000)
002A6410 0042:
	??_C@_0EC@MOKCAFPK@tag_block_add_element?$CI?$CJ?5is?5not?5s@ (0000)
002A6458 0045:
	??_C@_0EF@NIGPCFB@tag_block_delete_element?$CI?$CJ?5is?5no@ (0000)
002A64A0 0035:
	??_C@_0DF@IIJGOBIP@tag_load?$CI?$CJ?5is?5not?5supported?5with@ (0000)
002A64D8 0037:
	??_C@_0DH@OJBEKPBJ@tag_unload?$CI?$CJ?5is?5not?5supported?5wi@ (0000)
002A6510 003e:
	??_C@_0DO@CMGIBDCM@tag_file_get_path?$CI?$CJ?5is?5not?5suppo@ (0000)
002A6550 003e:
	??_C@_0DO@PNBFAACM@tag_reference_set?$CI?$CJ?5is?5not?5suppo@ (0000)
002A6590 0022:
	??_C@_0CC@JDEMIPEM@i?5don?8t?5think?5?$CF08x?5is?5a?5tag?5inde@ (0000)
002A65B4 0028:
	??_C@_0CI@MCDCHEHF@?8?$CFs?8?5does?5not?5appear?5to?5be?5a?5cac@ (0000)
002A65DC 0036:
	??_C@_0DG@MBMNLMG@the?5cache?5file?5?8?$CFs?8?5belongs?5to?5a@ (0000)
002A6614 0026:
	??_C@_0CG@GAKJDLAF@the?5cache?5file?5?8?$CFs?8?5is?5an?5old?5ve@ (0000)
002A663C 002e:
	??_C@_0CO@BLPPGPI@signature?5is?5?8?$CFc?$CFc?$CFc?$CFc?8?0?5should?5@ (0000)
002A666C 002b:
	??_C@_0CL@BCHNHKGI@tag_instance?9?$DOgroup_tag?$DN?$DNSTRUCTU@ (0000)
002A6698 001c:
	??_C@_0BM@EFCFDCHK@?$CBtag_instance?9?$DObase_address?$AA@ (0000)
002A66B8 005e:
	??_C@_0FO@FEJCGGNA@cache_file_globals?4structure_bsp@ (0000)
002A6718 001b:
	??_C@_0BL@OICFEJJN@tag_instance?9?$DObase_address?$AA@ (0000)
002A6734 0027:
	??_C@_0CH@HHANGOKG@can?8t?5get?$CI?$CJ?5a?5tag?5with?5a?5base?5ad@ (0000)
002A675C 002e:
	??_C@_0CO@IDEKIPEG@expected?5tag?5group?5?8?$CFs?8?5but?5got?5@ (0000)
00316820 0018:
	_data_00316820 (0000)
004CCB20 080c:
	_bss_004ccb20 (0000)
*/

/* ---------- headers */

#include "cseries.h"
#include "cseries_windows.h"
#include "errors.h"
#include "tag_files/tag_groups.h"
#include "tag_files/files.h"
#include "cache_files.h"
#include "physical_memory_map.h"
#include "sound_cache.h"
#include "scenario/scenario_definitions.h"
#include "hs/hs_scenario_definitions.h"
#include "sound/sound_manager.h"

/* ---------- constants */

/* ---------- macros */

#define STRUCTURE_BSP_TAG 'sbsp'
#define CACHE_FILE_TAG_HEADER_SIGNATURE 'tags'
#define CACHE_FILE_STRUCTURE_BSP_HEADER_SIGNATURE 'sbsp'
#define CACHE_FILE_HEADER_SIGNATURE 'head'
#define CACHE_FILE_FOOTER_SIGNATURE 'foot'

/* ---------- structures */

struct cache_file_tag_instance
{
	long group_tag;
	long parent_group_tags[2];
	long tag_index;
	char *name;
	void *base_address;
	unsigned long unused[2];
};

struct cache_file_tag_header
{
	struct cache_file_tag_instance *tag_instances;
	long scenario_tag_index;
	unsigned long checksum;
	long tag_count;
	long vertex_buffer_count;
	void *vertex_buffers;
	long index_buffer_count;
	void *index_buffers;
	unsigned long signature;
};

struct cache_file_structure_bsp_header
{
	void *base_address;
	long vertex_buffer_count;
	void *vertex_buffers;
	long index_buffer_count;
	void *index_buffers;
	unsigned long signature;
};

struct cache_file_header
{
	unsigned long header_signature;
	long version;
	long file_length;
	byte reservedC[4];
	long tag_data_offset;
	long tag_data_size;
	byte reserved18[8];
	char name[0x20];
	char build[0x20];
	byte reserved60[4];
	unsigned long checksum;
	byte reserved68[0x794];
	unsigned long footer_signature;
};

struct cache_file_globals
{
	boolean tags_loaded;
	byte pad1[3];
	struct cache_file_header header;
	struct cache_file_tag_header *tag_header;
	struct cache_file_structure_bsp_header *structure_bsp_header;
};

typedef char verify_cache_file_tag_instance_size[
	sizeof(struct cache_file_tag_instance) == 0x20 ? 1 : -1];

typedef char verify_cache_file_tag_header_count_offset[
	offsetof(struct cache_file_tag_header, tag_count) == 0xC ? 1 : -1];

typedef char verify_cache_file_globals_size[
	sizeof(struct cache_file_globals) == 0x80C ? 1 : -1];
typedef char verify_cache_file_header_size[
	sizeof(struct cache_file_header) == 0x800 ? 1 : -1];

/* ---------- prototypes */

static struct cache_file_tag_instance *cache_get_tag_instance(
	long tag_index);
void texture_cache_close(
	void);
void display_error_damaged_media(
	void);
void texture_cache_open(
	void);
void sound_idle(
	void);

/* ---------- globals */

struct cache_file_globals cache_file_globals = { 0 };
extern struct cache_file_tag_instance *global_tag_instances;
char const *data_00316820[] =
{
	"d:\\maps_de\\",
	"d:\\maps_fr\\",
	"d:\\maps_es\\",
	"d:\\maps_it\\",
	"d:\\maps\\",
	NULL
};

#ifdef HALO_VITA
#define XBOX_GAME_STATE_BASE 0x80061000UL
#define XBOX_GAME_STATE_SIZE 0x00345000UL
#define XBOX_TAG_CACHE_BASE  0x803A6000UL
#define XBOX_TAG_CACHE_SIZE  0x01600000UL

static boolean cache_file_vita_unaligned_string_pointer(
	void const *region,
	unsigned long region_bytes,
	unsigned long value)
{
	unsigned long offset;
	unsigned long limit;
	unsigned long index;
	boolean has_character = FALSE;
	byte const *string;

	if (value < XBOX_TAG_CACHE_BASE ||
		value >= XBOX_TAG_CACHE_BASE + region_bytes)
		return FALSE;

	offset = value - XBOX_TAG_CACHE_BASE;
	limit = MIN(256UL, region_bytes - offset);
	string = (byte const *)region + offset;
	for (index = 0; index < limit; index++)
	{
		byte character = string[index];
		if (!character)
			return has_character;
		if (character < 0x20 && character != '\t' && character != '\r' && character != '\n')
			return FALSE;
		if (character != ' ')
			has_character = TRUE;
	}

	return FALSE;
}

struct cache_file_vita_rebase_exclusion
{
	void const *address;
	unsigned long bytes;
};

/* These ranges contain serialized values, not relocatable pointers. Validate
 * them BEFORE rewriting the owning scenario's tag_block/tag_data addresses. */
static boolean cache_file_vita_exclude_range(
	struct cache_file_vita_rebase_exclusion *range,
	void *tag_cache, unsigned long tag_bytes,
	unsigned long raw_address, unsigned long count, unsigned long element_size)
{
	unsigned long offset;
	range->address = NULL;
	range->bytes = 0;
	if (!count)
		return TRUE;
	if (!element_size || raw_address < XBOX_TAG_CACHE_BASE)
		return FALSE;
	offset = raw_address-XBOX_TAG_CACHE_BASE;
	if (offset >= tag_bytes || count > (tag_bytes-offset)/element_size)
		return FALSE;
	range->address = (byte *)tag_cache+offset;
	range->bytes = count*element_size;
	return TRUE;
}

static void cache_file_rebase_vita_region(
	void *region,
	unsigned long region_bytes,
	void const *tag_string_region,
	unsigned long tag_string_bytes,
	struct cache_file_vita_rebase_exclusion const *exclusions,
	unsigned long exclusion_count,
	unsigned long tag_delta,
	unsigned long state_delta,
	unsigned long *tag_rebased,
	unsigned long *unaligned_strings_rebased,
	unsigned long *high_tag_rebased,
	unsigned long *state_rebased,
	unsigned long *excluded_words)
{
	unsigned long *words = (unsigned long *)region;
	unsigned long word_count = region_bytes / sizeof(*words);
	unsigned long region_address = (unsigned long)region;
	unsigned long index;

	for (index = 0; index < word_count; index++)
	{
		unsigned long word_address = region_address + index * sizeof(*words);
		unsigned long value = words[index];
		unsigned long range_index;
		for (range_index = 0; range_index < exclusion_count; range_index++)
		{
			unsigned long excluded_address = (unsigned long)exclusions[range_index].address;
			unsigned long excluded_bytes = exclusions[range_index].bytes;
			if (excluded_bytes && word_address < excluded_address + excluded_bytes &&
				word_address + sizeof(*words) > excluded_address)
				break;
		}
		if (range_index < exclusion_count)
		{
			if (excluded_words) (*excluded_words)++;
			continue;
		}

		if (value >= XBOX_TAG_CACHE_BASE &&
			value < XBOX_TAG_CACHE_BASE + XBOX_TAG_CACHE_SIZE &&
			(!(value & 3) || cache_file_vita_unaligned_string_pointer(tag_string_region, tag_string_bytes, value)))
		{
			if (value & 3)
				(*unaligned_strings_rebased)++;
			if (value >= XBOX_TAG_CACHE_BASE + region_bytes)
				(*high_tag_rebased)++;
			words[index] = value + tag_delta;
			(*tag_rebased)++;
		}
		else if (!(value & 3) && value >= XBOX_GAME_STATE_BASE &&
			value < XBOX_GAME_STATE_BASE + XBOX_GAME_STATE_SIZE)
		{
			words[index] = value + state_delta;
			(*state_rebased)++;
		}
	}
}

static boolean cache_file_rebase_vita_pointers(void *tag_cache, unsigned long tag_bytes)
{
	struct cache_file_tag_header *header = (struct cache_file_tag_header *)tag_cache;
	struct cache_file_tag_instance *instances;
	unsigned long tag_delta = (unsigned long)tag_cache - XBOX_TAG_CACHE_BASE;
	unsigned long state_base = (unsigned long)physical_memory_get_game_state_base_address();
	unsigned long state_delta = state_base - XBOX_GAME_STATE_BASE;
	unsigned long tag_rebased = 0;
	unsigned long unaligned_strings_rebased = 0;
	unsigned long high_tag_bases = 0;
	unsigned long state_rebased = 0;
	unsigned long syntax_words_excluded = 0;
	void *syntax_region = NULL;
	unsigned long syntax_bytes = 0;
	struct cache_file_vita_rebase_exclusion exclusions[4] = {{0}};
	unsigned long index;
	short scenario_absolute_index;
	FILE *log;

	if (!tag_cache || !state_base || !tag_bytes || tag_bytes > XBOX_TAG_CACHE_SIZE)
		return FALSE;
	if (header->tag_count <= 0 || header->tag_count > 65535 ||
		(unsigned long)header->tag_instances < XBOX_TAG_CACHE_BASE ||
		(unsigned long)header->tag_instances >= XBOX_TAG_CACHE_BASE + XBOX_TAG_CACHE_SIZE)
		return FALSE;

	/* The tag array and individual tag path/data fields are known pointers.
	 * Some stock Xbox UI tags live near the top of the complete 22 MiB tag
	 * window even though the serialized tag-data payload is much smaller.
	 * Relocate these structural fields against the complete window instead
	 * of treating them as arbitrary words in the payload. */
	header->tag_instances = (struct cache_file_tag_instance *)
		((unsigned long)header->tag_instances + tag_delta);
	tag_rebased++;
	instances = header->tag_instances;
	if ((unsigned long)header->tag_count >
			tag_bytes / sizeof(*instances) ||
		(byte *)instances < (byte *)tag_cache ||
		(byte *)instances > (byte *)tag_cache + tag_bytes -
			header->tag_count * sizeof(*instances))
		return FALSE;

	for (index = 0; index < (unsigned long)header->tag_count; index++)
	{
		unsigned long value = (unsigned long)instances[index].name;
		if (value >= XBOX_TAG_CACHE_BASE &&
			value < XBOX_TAG_CACHE_BASE + XBOX_TAG_CACHE_SIZE)
		{
			instances[index].name = (char *)(value + tag_delta);
			tag_rebased++;
		}

		value = (unsigned long)instances[index].base_address;
		if (value >= XBOX_TAG_CACHE_BASE &&
			value < XBOX_TAG_CACHE_BASE + XBOX_TAG_CACHE_SIZE)
		{
			if (value >= XBOX_TAG_CACHE_BASE + tag_bytes)
				high_tag_bases++;
			instances[index].base_address = (void *)(value + tag_delta);
			tag_rebased++;
		}
	}

	if ((unsigned long)header->vertex_buffers >= XBOX_TAG_CACHE_BASE &&
		(unsigned long)header->vertex_buffers < XBOX_TAG_CACHE_BASE + XBOX_TAG_CACHE_SIZE)
	{
		header->vertex_buffers = (void *)((unsigned long)header->vertex_buffers + tag_delta);
		tag_rebased++;
	}
	if ((unsigned long)header->index_buffers >= XBOX_TAG_CACHE_BASE &&
		(unsigned long)header->index_buffers < XBOX_TAG_CACHE_BASE + XBOX_TAG_CACHE_SIZE)
	{
		header->index_buffers = (void *)((unsigned long)header->index_buffers + tag_delta);
		tag_rebased++;
	}

	/* HS nodes AND script/global entry records contain datum handles rather
	 * than addresses. A valid handle such as 0x818f1e1c overlaps Xbox RAM.
	 * Preserve the complete value-only records and string bytes. The owning
	 * scenario's block pointers still go through the normal relocation pass.
	 * hs_scenario_postprocess resets the syntax data_array->data pointer. */
	scenario_absolute_index = (short)header->scenario_tag_index;
	if (header->scenario_tag_index != NONE &&
		scenario_absolute_index >= 0 &&
		scenario_absolute_index < header->tag_count)
	{
		struct cache_file_tag_instance *scenario_instance =
			&instances[scenario_absolute_index];
		unsigned long scenario_address = (unsigned long)scenario_instance->base_address;
		if (scenario_instance->tag_index == header->scenario_tag_index &&
			scenario_instance->group_tag == SCENARIO_TAG &&
			tag_bytes >= sizeof(struct scenario) &&
			scenario_address >= (unsigned long)tag_cache &&
			scenario_address <= (unsigned long)tag_cache + tag_bytes - sizeof(struct scenario))
		{
			struct scenario *scenario = (struct scenario *)scenario_address;
			if (!cache_file_vita_exclude_range(&exclusions[0], tag_cache, tag_bytes,
				(unsigned long)scenario->hs_syntax_data.address,
				(unsigned long)scenario->hs_syntax_data.size, 1) ||
				!cache_file_vita_exclude_range(&exclusions[1], tag_cache, tag_bytes,
				(unsigned long)scenario->hs_scripts.address,
				(unsigned long)scenario->hs_scripts.count, sizeof(struct hs_script)) ||
				!cache_file_vita_exclude_range(&exclusions[2], tag_cache, tag_bytes,
				(unsigned long)scenario->hs_globals.address,
				(unsigned long)scenario->hs_globals.count, sizeof(struct hs_global)) ||
				!cache_file_vita_exclude_range(&exclusions[3], tag_cache, tag_bytes,
				(unsigned long)scenario->hs_string_constants.address,
				(unsigned long)scenario->hs_string_constants.size, 1))
				return FALSE;
			syntax_region = (void *)exclusions[0].address;
			syntax_bytes = exclusions[0].bytes;
		}
	}

	/* Xbox cache data is a linked in-memory image.  Pointers stored in the
	 * serialized tag payload may target any part of the complete 22 MiB tag
	 * window, not merely the bytes present in the initial tag-data chunk.
	 * In particular, scenario BSP destinations live near the top of that
	 * window.  Leaving those addresses untouched makes the Vita stream the
	 * BSP into an unrelated Xbox-era virtual address and corrupts globals. */
	cache_file_rebase_vita_region(
		tag_cache,
		tag_bytes,
		tag_cache,
		tag_bytes,
		exclusions,
		4,
		tag_delta,
		state_delta,
		&tag_rebased,
		&unaligned_strings_rebased,
		&high_tag_bases,
		&state_rebased,
		&syntax_words_excluded);

	log = fopen("ux0:data/halo/boot.log", "a");
	if (log)
	{
		fprintf(log, "tag_rebase: tag_base=%p tag_bytes=%lu tag_pointers=%lu unaligned_string_pointers=%lu high_tag_pointers=%lu state_pointers=%lu hs_syntax=%p/%lu excluded_words=%lu\n",
			tag_cache, tag_bytes, tag_rebased, unaligned_strings_rebased, high_tag_bases, state_rebased,
			syntax_region, syntax_bytes, syntax_words_excluded);
		fprintf(log, "hs_rebase_preserved: scripts=%lu globals=%lu string_bytes=%lu\n",
			exclusions[1].bytes / (unsigned long)sizeof(struct hs_script),
			exclusions[2].bytes / (unsigned long)sizeof(struct hs_global), exclusions[3].bytes);
		if (!syntax_region)
		{
			fprintf(log, "tag_rebase: scenario_tag_index=%08lx absolute=%d scenario_instance_tag_index=%08lx group=%08lx base=%p\n",
				(unsigned long)header->scenario_tag_index, scenario_absolute_index,
				header->scenario_tag_index != NONE && scenario_absolute_index >= 0 &&
					scenario_absolute_index < header->tag_count ?
						(unsigned long)instances[scenario_absolute_index].tag_index : 0,
				header->scenario_tag_index != NONE && scenario_absolute_index >= 0 &&
					scenario_absolute_index < header->tag_count ?
						(unsigned long)instances[scenario_absolute_index].group_tag : 0,
				header->scenario_tag_index != NONE && scenario_absolute_index >= 0 &&
					scenario_absolute_index < header->tag_count ?
						instances[scenario_absolute_index].base_address : NULL);
		}
		fclose(log);
	}

	return tag_rebased > 0;
}

static boolean cache_file_vita_tag_pointer_valid(void const *pointer, unsigned long bytes)
{
	byte const *base = (byte const *)physical_memory_get_tag_cache_base_address();
	byte const *value = (byte const *)pointer;
	unsigned long tag_bytes = XBOX_TAG_CACHE_SIZE;

	return base && value && value >= base && bytes <= tag_bytes &&
		(unsigned long)(value - base) <= tag_bytes - bytes;
}
#endif

/* ---------- private code */

static struct cache_file_tag_instance *cache_get_tag_instance(
	long tag_index)
{
	short absolute_index;
	struct cache_file_tag_instance *tag_instance;

	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		518,
		cache_file_globals.tags_loaded);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		519,
		global_tag_instances);

	absolute_index = (short)tag_index;
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		522,
		absolute_index >= 0 && absolute_index < cache_file_globals.tag_header->tag_count,
		csprintf(temporary, "i don't think %08x is a tag index", tag_index));

	tag_instance = &global_tag_instances[absolute_index];
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		526,
		!(tag_index & 0xFFFF0000) || tag_instance->tag_index == tag_index,
		csprintf(temporary, "i don't think %08x is a tag index", tag_index));

	return tag_instance;
}

/* ---------- public code */

char const *cache_files_map_directory(
	void)
{
	char const *map_directory;
	struct file_reference reference;
	long directory_index;

	switch (XGetLanguage())
	{
	case XC_LANGUAGE_GERMAN:
		map_directory = "d:\\maps_de\\";
		break;
	case XC_LANGUAGE_FRENCH:
		map_directory = "d:\\maps_fr\\";
		break;
	case XC_LANGUAGE_SPANISH:
		map_directory = "d:\\maps_es\\";
		break;
	case XC_LANGUAGE_ITALIAN:
		map_directory = "d:\\maps_it\\";
		break;
	default:
		map_directory = "d:\\maps\\";
		break;
	}

	if (!file_exists(file_reference_create_from_path(&reference, map_directory, TRUE)))
	{
		for (directory_index = 0; data_00316820[directory_index]; directory_index++)
		{
			if (file_exists(file_reference_create_from_path(
				&reference,
				data_00316820[directory_index],
				TRUE)))
			{
				map_directory = data_00316820[directory_index];
				break;
			}
		}

		match_vassert(
			"c:\\halo\\SOURCE\\cache\\cache_files.c",
			60,
			data_00316820[directory_index],
			"no valid map directory exists");
	}

	return map_directory;
}

void scenario_tags_unload(
	void)
{
	sound_cache_close();
	texture_cache_close();
	cache_file_close();
	tags_header_deregister_vertex_and_index_buffers(cache_file_globals.tag_header);
	cache_file_globals.tags_loaded = FALSE;
	global_tag_instances = NULL;

	return;
}

void tag_files_open(
	void)
{
	cache_files_initialize();

	return;
}

void tag_files_close(
	void)
{
	cache_files_dispose();

	return;
}

unsigned long cache_files_get_checksum(
	void)
{
	return cache_file_globals.header.checksum;
}

unsigned long tag_groups_checksum(
	void)
{
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		327,
		cache_file_globals.tags_loaded);

	return cache_file_globals.tag_header->checksum;
}

long tag_loaded(
	long group_tag,
	char const *name)
{
	short absolute_index;
	long result = NONE;

	if (cache_file_globals.tags_loaded)
	{
		match_assert(
			"c:\\halo\\SOURCE\\cache\\cache_files.c",
			346,
			global_tag_instances);

		for (absolute_index = 0;
			absolute_index < cache_file_globals.tag_header->tag_count;
			absolute_index++)
		{
			char const *instance_name = global_tag_instances[absolute_index].name;
#ifdef HALO_VITA
			if (!cache_file_vita_tag_pointer_valid(instance_name, 1) ||
				!memchr(instance_name, 0,
					XBOX_TAG_CACHE_SIZE -
					(unsigned long)((byte const *)instance_name -
						(byte const *)physical_memory_get_tag_cache_base_address())))
			{
				continue;
			}
#endif
			if (group_tag == global_tag_instances[absolute_index].group_tag &&
				!_stricmp(name, instance_name))
			{
				result = global_tag_instances[absolute_index].tag_index;
				break;
			}
		}
	}

	return result;
}

void cache_files_enable_writes(
	void)
{
#ifdef HALO_VITA
	XPhysicalProtect(physical_memory_get_tag_cache_base_address(), 0x01600000, PAGE_READWRITE);
#else
	XPhysicalProtect((void *)0x803A6000, 0x01600000, PAGE_READWRITE);
#endif

	return;
}

void cache_files_disable_writes(
	void)
{
#ifdef HALO_VITA
	XPhysicalProtect(physical_memory_get_tag_cache_base_address(), 0x01600000, PAGE_READONLY);
#else
	XPhysicalProtect((void *)0x803A6000, 0x01600000, PAGE_READONLY);
#endif
	XPhysicalProtect(
		cache_file_globals.tag_header->vertex_buffers,
		cache_file_globals.tag_header->vertex_buffer_count * 12,
		PAGE_READWRITE);
	XPhysicalProtect(
		cache_file_globals.tag_header->index_buffers,
		cache_file_globals.tag_header->index_buffer_count * 12,
		PAGE_READWRITE);

	if (cache_file_globals.structure_bsp_header)
	{
		XPhysicalProtect(
			cache_file_globals.structure_bsp_header->vertex_buffers,
			cache_file_globals.structure_bsp_header->vertex_buffer_count * 12,
			PAGE_READWRITE);
		XPhysicalProtect(
			cache_file_globals.structure_bsp_header->index_buffers,
			cache_file_globals.structure_bsp_header->index_buffer_count * 12,
			PAGE_READWRITE);
	}

	return;
}

boolean tag_block_resize(
	struct tag_block *block,
	long count)
{
	error(_error_silent, "tag_block_resize() is not supported with a cache file active");

	return FALSE;
}

boolean tag_data_resize(
	struct tag_data *data,
	long size)
{
	error(_error_silent, "tag_data_resize() is not supported with a cache file active");

	return FALSE;
}

long tag_block_add_element(
	struct tag_block *block)
{
	error(_error_silent, "tag_block_add_element() is not supported with a cache file active");

	return NONE;
}

void tag_block_delete_element(
	struct tag_block *block,
	long element_index)
{
	error(_error_silent, "tag_block_delete_element() is not supported with a cache file active");

	return;
}

long tag_load(
	long group_tag,
	char const *name,
	unsigned long flags)
{
	error(_error_silent, "tag_load() is not supported with a cache file active");

	return NONE;
}

void tag_unload(
	long tag_index)
{
	error(_error_silent, "tag_unload() is not supported with a cache file active");

	return;
}

void tag_file_get_path(
	long group_tag,
	char const *name,
	char *path)
{
	error(_error_silent, "tag_file_get_path() is not supported with a cache file active");
	path[0] = 0;

	return;
}

void tag_reference_set(
	struct tag_reference *reference,
	unsigned long group_tag,
	char const *name)
{
	error(_error_silent, "tag_reference_set() is not supported with a cache file active");

	return;
}

void tag_iterator_new(
	struct tag_iterator *iterator,
	long group_tag)
{
	iterator->absolute_index = 0;
	iterator->group_tag = group_tag;

	return;
}

long tag_iterator_next(
	struct tag_iterator *iterator)
{
	long result = NONE;

	while (iterator->absolute_index < cache_file_globals.tag_header->tag_count)
	{
		struct cache_file_tag_instance *tag_instance =
			&global_tag_instances[iterator->absolute_index++];

		if (tag_instance &&
			(iterator->group_tag == NONE ||
			iterator->group_tag == tag_instance->group_tag ||
			iterator->group_tag == tag_instance->parent_group_tags[0] ||
			iterator->group_tag == tag_instance->parent_group_tags[1]))
		{
			result = tag_instance->tag_index;
			break;
		}
	}

	return result;
}

boolean cache_file_header_verify(
	struct cache_file_header *header,
	char const *scenario_name,
	boolean fatal)
{
#ifdef HALO_VITA
	{
		FILE *log = fopen("ux0:data/halo/boot.log", "a");
		if (log) {
			fprintf(log, "cache_header: requested='%.255s' name='%.32s' head=%08lx foot=%08lx bytes=%ld version=%ld fatal=%d\n",
				scenario_name ? scenario_name : "<null>", header->name,
				header->header_signature, header->footer_signature, header->file_length,
				(long)header->version, (int)fatal);
			fclose(log);
		}
	}
#endif
	if (header->header_signature != CACHE_FILE_HEADER_SIGNATURE ||
		header->footer_signature != CACHE_FILE_FOOTER_SIGNATURE ||
		header->file_length < 0 ||
		header->file_length > 0x11600000 ||
#ifdef HALO_VITA
		!memchr(header->name, 0, sizeof(header->name)))
#else
		csstrlen(header->name) > 31)
#endif
	{
		if (fatal)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				544,
				FALSE,
				csprintf(temporary, "'%s' does not appear to be a cache file", scenario_name));
		}

		return FALSE;
	}

	if (header->version != 5)
	{
		if (fatal)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				548,
				FALSE,
				csprintf(temporary, "the cache file '%s' is an old version", scenario_name));
		}

		return FALSE;
	}

#ifndef HALO_LINUX
	/* (the native builds try a cache file whatever build made it, NTSC's
	01.10.12.2276 included) */
	if (csstrcmp(header->build, "01.01.14.2342"))
	{
		if (fatal)
		{
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				553,
				FALSE,
				csprintf(
					temporary,
					"the cache file '%s' belongs to a different build (%s)",
					header->name,
					header->build));
		}

		return FALSE;
	}
#endif

	return TRUE;
}

boolean cache_files_give_time_to_precache(
	char const *map_name)
{
	boolean result = FALSE;

	if (cache_files_precache_map_loaded(map_name))
	{
		result = TRUE;
	}
	else
	{
		if (cache_files_precache_in_progress() &&
			!cache_files_precache_is_copying_map(map_name))
		{
			cache_files_precache_map_end();
		}

		if (cache_files_precache_in_progress())
		{
			real progress;
			short status = cache_files_precache_map_status(&progress);

			if (status == 2)
				display_error_damaged_media();
			else if (status == 1)
				cache_files_precache_map_end();
		}
		else
		{
			cache_files_precache_set_priority(0);
			if (!cache_files_precache_map_begin(map_name, FALSE))
				display_error_damaged_media();
		}
	}

	return result;
}

long scenario_tags_load(
	char const *scenario_name)
{
	long result;
	char const *stripped_scenario_name;
	void *tag_cache_base_address;
	boolean read_complete;

	stripped_scenario_name = tag_name_strip_path(scenario_name);
	result = NONE;
#ifdef HALO_VITA
	{
		FILE *log = fopen("ux0:data/halo/boot.log", "a");
		if (log) { fprintf(log, "campaign_load: scenario_tags_load name=%p '%.255s' stripped='%.255s'\n", scenario_name, scenario_name, stripped_scenario_name); fclose(log); }
	}
#endif
	texture_cache_open();
	sound_cache_open();
	if (cache_file_open(stripped_scenario_name, &cache_file_globals.header))
	{
		tag_cache_base_address = physical_memory_get_tag_cache_base_address();
		if (cache_file_header_verify(&cache_file_globals.header, scenario_name, TRUE))
		{
			csmemset(tag_cache_base_address, 0xCD, 0x01600000);
			cache_file_read(
				NONE,
				cache_file_globals.header.tag_data_offset,
				cache_file_globals.header.tag_data_size,
				tag_cache_base_address,
				&read_complete,
				TRUE);
			while (!read_complete)
			{
				SwitchToThread();
			}

			cache_file_globals.tag_header = tag_cache_base_address;
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				0x94,
				cache_file_globals.tag_header->signature == CACHE_FILE_TAG_HEADER_SIGNATURE,
				csprintf(
					temporary,
					"signature is '%c%c%c%c', should be '%c%c%c%c'",
					((char *)&cache_file_globals.tag_header->signature)[3],
					((char *)&cache_file_globals.tag_header->signature)[2],
					((char *)&cache_file_globals.tag_header->signature)[1],
					((char *)&cache_file_globals.tag_header->signature)[0],
					't',
					'a',
					'g',
					's'));
#ifdef HALO_VITA
			match_vassert(
				"c:\\halo\\SOURCE\\cache\\cache_files.c",
				0x94,
				cache_file_rebase_vita_pointers(tag_cache_base_address, cache_file_globals.header.tag_data_size),
				"Vita tag pointer rebase failed");
#endif
			global_tag_instances = cache_file_globals.tag_header->tag_instances;
			tags_header_register_vertex_and_index_buffers(cache_file_globals.tag_header);
			cache_file_globals.tags_loaded = TRUE;
			result = cache_file_globals.tag_header->scenario_tag_index;
		}
	}

	return result;
}

boolean scenario_structure_bsp_load(
	struct scenario_structure_bsp_reference *reference)
{
	struct cache_file_tag_instance *tag_instance;
	byte *tag_cache_base_address;
#ifdef HALO_VITA
	unsigned long bsp_tag_rebased = 0;
	unsigned long bsp_unaligned_strings_rebased = 0;
	unsigned long bsp_high_tag_rebased = 0;
	unsigned long bsp_state_rebased = 0;
	unsigned long tag_delta;
	unsigned long state_delta;
	FILE *vita_log;
#endif

	tag_cache_base_address = physical_memory_get_tag_cache_base_address();
#ifdef HALO_VITA
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xD1,
		reference->file_size > 0 &&
		cache_file_vita_tag_pointer_valid(reference->base_address,
			(unsigned long)reference->file_size),
		csprintf(temporary,
			"BSP destination %p (%ld bytes) was not rebased into the Vita tag cache",
			reference->base_address,
			reference->file_size));
#endif
	csmemset(
		tag_cache_base_address + cache_file_globals.header.tag_data_size,
		0xCD,
		0x01600000 - cache_file_globals.header.tag_data_size);
	{
		boolean read_complete;

		cache_file_read(
			NONE,
			reference->file_offset,
			reference->file_size,
			reference->base_address,
			&read_complete,
			TRUE);
		while (!read_complete)
		{
			SwitchToThread();
			if (system_milliseconds() - sound_render_time() > 33)
			{
				sound_idle();
			}
		}
	}

#ifdef HALO_VITA
	/* The BSP is another Xbox linked-memory image.  It is read after the
	 * initial tag rebase, so translate its internal pointers separately once
	 * the asynchronous read has completed. */
	tag_delta = (unsigned long)tag_cache_base_address - XBOX_TAG_CACHE_BASE;
	state_delta = (unsigned long)physical_memory_get_game_state_base_address() -
		XBOX_GAME_STATE_BASE;
	cache_file_rebase_vita_region(
		reference->base_address,
		reference->file_size,
		tag_cache_base_address,
		cache_file_globals.header.tag_data_size,
		NULL,
		0,
		tag_delta,
		state_delta,
		&bsp_tag_rebased,
		&bsp_unaligned_strings_rebased,
		&bsp_high_tag_rebased,
		&bsp_state_rebased,
		NULL);
	vita_log = fopen("ux0:data/halo/boot.log", "a");
	if (vita_log)
	{
		fprintf(vita_log,
			"bsp_rebase: destination=%p bytes=%ld tag_pointers=%lu unaligned_string_pointers=%lu high_tag_pointers=%lu state_pointers=%lu\n",
			reference->base_address,
			reference->file_size,
			bsp_tag_rebased,
			bsp_unaligned_strings_rebased,
			bsp_high_tag_rebased,
			bsp_state_rebased);
		fclose(vita_log);
	}
#endif

	cache_file_globals.structure_bsp_header = reference->base_address;
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xE0,
		cache_file_globals.structure_bsp_header->signature==CACHE_FILE_STRUCTURE_BSP_HEADER_SIGNATURE);
	structure_bsp_header_register_vertex_buffers(cache_file_globals.structure_bsp_header);
	tag_instance = cache_get_tag_instance(reference->structure_bsp.index);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xEA,
		!tag_instance->base_address);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		0xEB,
		tag_instance->group_tag==STRUCTURE_BSP_TAG);
	tag_instance->base_address = cache_file_globals.structure_bsp_header->base_address;

	return TRUE;
}

void scenario_structure_bsp_unload(
	struct scenario_structure_bsp_reference *reference)
{
	struct cache_file_tag_instance *tag_instance;

	structure_bsp_header_deregister_vertex_buffers(cache_file_globals.structure_bsp_header);
	tag_instance = cache_get_tag_instance(reference->structure_bsp.index);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		256,
		tag_instance->base_address);
	match_assert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		257,
		tag_instance->group_tag==STRUCTURE_BSP_TAG);
	tag_instance->base_address = NULL;
	cache_file_globals.structure_bsp_header = NULL;

	return;
}

void *tag_get(
	long group_tag,
	long tag_index)
{
	char expected_group[16];
	char returned_group[16];

	struct cache_file_tag_instance *tag_instance = cache_get_tag_instance(tag_index);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		298,
		tag_instance->group_tag == group_tag ||
		tag_instance->parent_group_tags[0] == group_tag ||
		tag_instance->parent_group_tags[1] == group_tag,
		csprintf(
			temporary,
			"expected tag group '%s' but got '%s' for %08x",
			tag_to_string(group_tag, expected_group),
			tag_to_string(tag_instance->group_tag, returned_group),
			tag_index)
	);
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		302,
		tag_instance->base_address,
		csprintf(temporary, "can't get() a tag with a base address!")
	);
#ifdef HALO_VITA
	match_vassert(
		"c:\\halo\\SOURCE\\cache\\cache_files.c",
		303,
		cache_file_vita_tag_pointer_valid(tag_instance->base_address, 1),
		csprintf(temporary, "tag %08x has an out-of-range Vita base address %p",
			tag_index, tag_instance->base_address));
#endif
	
	return tag_instance->base_address;
}

#ifdef HALO_LINUX
/* whether the index is a loaded tag of the group (or a group it inherits
from): the distributed netcode names tags another machine sent
(port/linux/game/network_damage.c), which tag_get would only assert on */
boolean tag_index_is_group(
	long tag_index,
	long group_tag)
{
	short absolute_index = (short)tag_index;
	struct cache_file_tag_instance *tag_instance;

	if (tag_index == NONE || !cache_file_globals.tags_loaded || !global_tag_instances ||
		absolute_index < 0 || absolute_index >= cache_file_globals.tag_header->tag_count)
	{
		return FALSE;
	}
	tag_instance = &global_tag_instances[absolute_index];
	return tag_instance->tag_index == tag_index && tag_instance->base_address &&
		(tag_instance->group_tag == group_tag || tag_instance->parent_group_tags[0] == group_tag ||
			tag_instance->parent_group_tags[1] == group_tag);
}

#endif
char *tag_get_name(
	long tag_index)
{
	return cache_get_tag_instance(tag_index)->name;
}

unsigned long tag_get_group_tag(
	long tag_index)
{
	return cache_get_tag_instance(tag_index)->group_tag;
}
