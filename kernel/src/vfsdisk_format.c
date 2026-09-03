#include "vfsdisk_format.h"

#define NODE_BYTES VFSDISK_NODE_BYTES
#define DATA_AT    64u

_Static_assert(VFS_NAME_MAX <= DATA_AT - 40, "the name has to fit before the fields");
_Static_assert(DATA_AT + VFS_FILE_MAX <= NODE_BYTES, "a node has to fit its record");

static void put32(uint8_t *at, uint32_t value)
{
    for (uint64_t i = 0; i < 4; i++) {
        at[i] = (uint8_t)(value >> (8 * i));
    }
}

static uint32_t get32(const uint8_t *at)
{
    uint32_t value = 0;
    for (uint64_t i = 0; i < 4; i++) {
        value |= (uint32_t)at[i] << (8 * i);
    }
    return value;
}

static void put16(uint8_t *at, int16_t value)
{
    const uint16_t raw = (uint16_t)value;
    at[0] = (uint8_t)raw;
    at[1] = (uint8_t)(raw >> 8);
}

static int16_t get16(const uint8_t *at)
{
    return (int16_t)((uint16_t)at[0] | (uint16_t)((uint16_t)at[1] << 8));
}

static void fill(uint8_t *at, uint8_t value, uint64_t count)
{
    for (uint64_t i = 0; i < count; i++) {
        at[i] = value;
    }
}

void vfsdisk_write_header(uint8_t *sector)
{
    fill(sector, 0, DISK_SECTOR);
    for (uint64_t i = 0; i < 8; i++) {
        sector[i] = (uint8_t)VFSDISK_MAGIC[i];
    }
    put32(sector + 8, VFSDISK_VERSION);
    put32(sector + 12, VFS_MAX_NODES);
    put32(sector + 16, VFS_FILE_MAX);
    put32(sector + 20, VFS_NAME_MAX);
    put32(sector + 24, VFSDISK_NODE_SECTORS);
}

/* Which of the header's promises this build can keep. Split from the reading so
 * the order of the checks is one thing to look at: a disk that is not ours at
 * all is not the same news as one of ours we cannot read. */
enum vfsdisk_result vfsdisk_judge_header(const uint8_t *sector)
{
    for (uint64_t i = 0; i < 8; i++) {
        if (sector[i] != (uint8_t)VFSDISK_MAGIC[i]) {
            return VFSDISK_NOT_FORMATTED;
        }
    }
    if (get32(sector + 8) != VFSDISK_VERSION) {
        return VFSDISK_WRONG_VERSION;
    }
    if (get32(sector + 12) != VFS_MAX_NODES ||
        get32(sector + 16) != VFS_FILE_MAX ||
        get32(sector + 20) != VFS_NAME_MAX ||
        get32(sector + 24) != VFSDISK_NODE_SECTORS) {
        return VFSDISK_WRONG_SHAPE;
    }
    return VFSDISK_OK;
}

void vfsdisk_write_node(uint8_t *record, const struct vfs_node *node)
{
    fill(record, 0, NODE_BYTES);
    /* A free node is written as nothing at all. `vfs_init` marks a node free
     * without clearing it, so its old name and its old contents are still
     * sitting there, and writing them out would put deleted files on the disk
     * where somebody could read them back. */
    if (!node->used) {
        return;
    }
    for (uint64_t i = 0; i < VFS_NAME_MAX; i++) {
        record[i] = (uint8_t)node->name[i];
    }
    record[24] = node->kind == VFS_FILE ? 1u : 0u;
    record[25] = node->used ? 1u : 0u;
    put16(record + 26, node->parent);
    put16(record + 28, node->first_child);
    put16(record + 30, node->next_sibling);
    put32(record + 32, node->length);

    /* Only what the file holds. The rest of the block is whatever was in memory
     * and putting it on the disk would write the ends of deleted files into a
     * place somebody can read them back out of. */
    const uint32_t length =
        node->length > VFS_FILE_MAX ? VFS_FILE_MAX : node->length;
    for (uint32_t i = 0; i < length; i++) {
        record[DATA_AT + i] = (uint8_t)node->data[i];
    }
}

void vfsdisk_read_node(const uint8_t *record, struct vfs_node *node)
{
    for (uint64_t i = 0; i < VFS_NAME_MAX; i++) {
        node->name[i] = (char)record[i];
    }
    node->kind = record[24] == 0 ? VFS_DIR : VFS_FILE;
    node->used = record[25] != 0;
    node->parent = get16(record + 26);
    node->first_child = get16(record + 28);
    node->next_sibling = get16(record + 30);
    node->length = get32(record + 32);

    const uint32_t length =
        node->length > VFS_FILE_MAX ? VFS_FILE_MAX : node->length;
    for (uint32_t i = 0; i < VFS_FILE_MAX; i++) {
        node->data[i] = i < length ? (char)record[DATA_AT + i] : '\0';
    }
}
