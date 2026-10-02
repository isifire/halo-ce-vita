/* vita-elf-create appends module/import metadata to the executable segment.
 * Keep room before the writable segment regardless of text/rodata size.
 */
SECTIONS
{
    . = ALIGN(0x1000) + 0x10000;
}
INSERT BEFORE .init_array;
