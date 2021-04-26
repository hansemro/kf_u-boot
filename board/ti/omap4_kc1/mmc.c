/*
 * Copyright (c) 2010, The Android Open Source Project.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Neither the name of The Android Open Source Project nor the names
 *    of its contributors may be used to endorse or promote products
 *    derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 */

#include <common.h>
#include <asm/omap_common.h>
#include <asm/io.h>
#include <mmc.h>
#include <malloc.h>
#ifdef CONFIG_CMD_FASTBOOT
#include <usb/fastboot.h>
#endif
#include <asm/arch/mmc_host_def.h>
#include <linux/ctype.h>
#include <asm/sizes.h>
#include <lcd.h>

#include "kc1_twl6030.h"

#define EFI_VERSION 0x00010000
#define EFI_ENTRIES 128
#define EFI_NAMELEN 36
#define MMC_DEVICE 1
#define MMC_BLOCK_SIZE		512	//1 page <=> 512 bytes

//#define DEBUG
#ifdef DEBUG
#define DBG(x...) printf(x)
#else
#define DBG(x...)
#endif /* DEBUG */

/* IDME VALUES */
static char          idme_serial[20];
static char          idme_macaddr[20];
static unsigned int  idme_settings;

static int load_ptbl(void);
int do_mmcops(cmd_tbl_t *cmdtp, int flag, int argc, char *argv[]);


static const u8 partition_type[16] = {
	0xa2, 0xa0, 0xd0, 0xeb, 0xe5, 0xb9, 0x33, 0x44,
	0x87, 0xc0, 0x68, 0xb6, 0xb7, 0x26, 0x99, 0xc7,
};

static const u8 random_uuid[16] = {
	0xff, 0x1f, 0xf2, 0xf9, 0xd4, 0xa8, 0x0e, 0x5f,
	0x97, 0x46, 0x59, 0x48, 0x69, 0xae, 0xc3, 0x4e,
};


struct efi_entry {
	u8 type_uuid[16];
	u8 uniq_uuid[16];
	u64 first_lba;
	u64 last_lba;
	u64 attr;
	u16 name[EFI_NAMELEN];
};

struct efi_header {
	u8 magic[8];

	u32 version;
	u32 header_sz;

	u32 crc32;
	u32 reserved;

	u64 header_lba;
	u64 backup_lba;
	u64 first_lba;
	u64 last_lba;

	u8 volume_uuid[16];

	u64 entries_lba;

	u32 entries_count;
	u32 entries_size;
	u32 entries_crc32;
} __attribute__((packed));

struct ptable {
	u8 mbr[512];
	union {
		struct efi_header header;
		u8 block[512];
	};
	struct efi_entry entry[EFI_ENTRIES];
};


typedef struct nvram_t {
    const char *name;
    unsigned int offset;
    unsigned int size;
} nvram_t;

static const struct nvram_t nvram_info[] = {
    {	.name = "serial",	.offset = 0x0,		.size = 16,	},
    {	.name = "mac",		.offset = 0x30,		.size = 12,	},
    {	.name = "sec",		.offset = 0x40,		.size = 20,	},
    {	.name = "pcbsn",	.offset = 0x60,		.size = 16,	},
    {	.name = "bootmode",	.offset = 0x1000,	.size = 16,	},
    {	.name = "postmode",	.offset = 0x1010,	.size = 16,	},
    {	.name = "bootcounter",	.offset = 0x1020,	.size = 16,	},
    {	.name = "lpddr2",	.offset = 0x2000,	.size = 16,	},
    {	.name = "emmc",		.offset = 0x2020,	.size = 16,	},
    {	.name = "product",	.offset = 0x2040,	.size = 16,	},
    {	.name = "settings",	.offset = 0x2060,	.size = 2,	},
};

#define CONFIG_NUM_NV_VARS (sizeof(nvram_info)/sizeof(nvram_info[0]))

static void init_mbr(u8 *mbr, u32 blocks)
{
	mbr[0x1be] = 0x00; /* nonbootable */
	mbr[0x1bf] = 0xFF; /* bogus CHS */
	mbr[0x1c0] = 0xFF;
	mbr[0x1c1] = 0xFF;

	mbr[0x1c2] = 0xEE; /* GPT partition */
	mbr[0x1c3] = 0xFF; /* bogus CHS */
	mbr[0x1c4] = 0xFF;
	mbr[0x1c5] = 0xFF;

	mbr[0x1c6] = 0x01; /* start */
	mbr[0x1c7] = 0x00;
	mbr[0x1c8] = 0x00;
	mbr[0x1c9] = 0x00;

	blocks = (blocks > 0xFFFFFFFF) ? 0xFFFFFFFF : blocks;
	memcpy(mbr + 0x1ca, &blocks, sizeof(u32));

	mbr[0x1fe] = 0x55;
	mbr[0x1ff] = 0xaa;
}
static void start_ptbl(struct ptable *ptbl, u64 blocks)
{
	struct efi_header *hdr = &ptbl->header;

	DBG("start_ptbl\n");

	memset(ptbl, 0, sizeof(*ptbl));

	init_mbr(ptbl->mbr, blocks - 1);

	memcpy(hdr->magic, "EFI PART", 8);
	hdr->version = EFI_VERSION;
	hdr->header_sz = sizeof(struct efi_header);
	hdr->crc32 = 0;
	hdr->reserved = 0;
	hdr->header_lba = 1;
	hdr->backup_lba = blocks - 1;
	hdr->first_lba = 34;
	hdr->last_lba = blocks - 1;
	memcpy(hdr->volume_uuid, random_uuid, 16);
	hdr->entries_lba = 2;
	hdr->entries_count = EFI_ENTRIES;
	hdr->entries_size = sizeof(struct efi_entry);
	hdr->entries_crc32 = 0;

	DBG("magic		= %s \n",  hdr->magic);
	DBG("version		= %u \n",  hdr->version);
	DBG("header_sz	= %u \n",  hdr->header_sz);
	DBG("crc32		= %u \n",  hdr->crc32);
	DBG("reserved	= %u \n",  hdr->reserved);
	DBG("header_lba	= %llu \n",  hdr->header_lba);
	DBG("backup_lba	= %llu \n",  hdr->backup_lba);
	DBG("first_lba	= %llu \n",  hdr->first_lba);
	DBG("last_lba	= %llu \n",  hdr->last_lba);
	DBG("entries_lba	= %llu \n",  hdr->entries_lba);
	DBG("entries_count	= %u \n",  hdr->entries_count);
	DBG("entries_size	= %u \n",  hdr->entries_size);
	DBG("entries_crc32	= %u \n",  hdr->entries_crc32);
}

static void end_ptbl(struct ptable *ptbl)
{
	struct efi_header *hdr = &ptbl->header;
	u32 n;

	n = crc32(0, 0, 0);
	n = crc32(n, (void*) ptbl->entry, sizeof(ptbl->entry));
	hdr->entries_crc32 = n;

	n = crc32(0, 0, 0);
	n = crc32(0, (void*) &ptbl->header, sizeof(ptbl->header));
	hdr->crc32 = n;
}

static int add_ptn(struct ptable *ptbl, u64 first, u64 last, const char *name)
{
	struct efi_header *hdr = &ptbl->header;
	struct efi_entry *entry = ptbl->entry;
	u32 n; int i = 0;

	if (first < 34) {
		printf("partition '%s' overlaps partition table\n", name);
		return -1;
	}

	if (last > hdr->last_lba) {
		printf("partition '%s' does not fit\n", name);
		return -1;
	}

	for (n = 0; n < EFI_ENTRIES; n++, entry++) {
		if (entry->last_lba)
			continue;
		memcpy(entry->type_uuid, partition_type, 16);
		memcpy(entry->uniq_uuid, random_uuid, 16);
		entry->uniq_uuid[0] = n;
		entry->first_lba = first;
		entry->last_lba = last;

		/* Converting partition name to simple unicode
		as expected by the kernel */
		while (i <= EFI_NAMELEN && *name) {
			entry->name[i] = name[i];
			if (name[i] == 0)
				break;
			entry->name[i+1] = '0';
			i++;
		}

		return 0;
	}

	return -1;
}

#ifdef CONFIG_CMD_FASTBOOT
static void import_efi_partition(struct efi_entry *entry, int count)
{
	struct fastboot_ptentry e;
	int ret = 0;
	int n;

	ret = memcmp(entry->type_uuid, partition_type, sizeof(partition_type));
	if (ret != 0) {
		//DBG("memcmp failed for count=%d, ret = %d. entry->type_uuid"
		//    "and partition_type are mismatched.\n", count, ret);
		return;
	}

	for (n = 0; n < (sizeof(e.name)-1); n++) {
		e.name[n] = entry->name[n];
	}
	e.name[n] = 0;


	e.start = entry->first_lba;
	e.length = (entry->last_lba - entry->first_lba + 1) * MMCSD_SECTOR_SIZE;
	e.flags = 0;

// no env on kc1
#if 0
	if (!strcmp(e.name, "environment"))
		e.flags |= FASTBOOT_PTENTRY_FLAGS_WRITE_ENV;
#endif

	fastboot_flash_add_ptn(&e, count);


#ifdef DEBUG
	if (e.length > 0x100000)
		DBG("%8d %8lluM %s\n", e.start,
			(e.length/(u64)0x100000), e.name);
	else
		DBG("%8d %7dK %s\n", e.start,
			(u32)(e.length/0x400), e.name);
#endif

}
#endif

static int load_ptbl(void)
{
	u64 ptbl_sectors = 0;
#ifdef CONFIG_CMD_FASTBOOT
	int i = 0;
#endif
	int r = 0;

	struct ptable *gpt;
	struct mmc* mmc = NULL;

	mmc = find_mmc_device(MMC_DEVICE);
	if (mmc == NULL) {
		printf("No MMC in slot 1\n");
		return -1;
	}
	mmc->has_init = 0;
	mmc_init(mmc);
	if (r!= 0) {
		printf("mmc init failed\n");
		return r;
	}

	int gpt_size = sizeof(struct ptable);

	gpt =  (struct ptable *) malloc(gpt_size);
	if (!gpt) {
		r = -1;
		goto fail;
	}

	ptbl_sectors = (u64)(gpt_size / MMCSD_SECTOR_SIZE);

	r = mmc->block_dev.block_read(1, 0, ptbl_sectors,( void*)gpt);
	if (r == -1) {
		printf("error reading GPT\n");
		goto fail;
	}

	if (memcmp(gpt->header.magic, "EFI PART", 8)) {
		printf("efi partition table not found\n");
		r = -1;
		goto fail;
	}

#ifdef CONFIG_CMD_FASTBOOT
	for (i = 0; i < EFI_ENTRIES; i++)
		import_efi_partition(&gpt->entry[i], i);
#endif

fail:
	free((void *)gpt);
	return r;
}

char *convert_ptn_name_to_unicode(struct efi_entry *entry)
{
	int i = 0;
	static char name[16];

	/* copying a simple unicode partition name */
	while (i < (sizeof(entry->name)-1)) {
		name[i] = entry->name[i];
		i++;
		if (entry->name[i] == 0)
			break;
	}

	name[i] = 0;

	return name;
}

static u64 get_entry_size_kb(struct efi_entry *entry, const char *ptn)
{
	int ret = 0;
	char name[16];
	u64 sz = 0;

	ret = memcmp(entry->type_uuid, partition_type, sizeof(partition_type));
	if (ret != 0)
		return 0;

	strcpy(name, (convert_ptn_name_to_unicode(entry)));

	if (!strcmp(name, ptn))
		sz = (entry->last_lba - entry->first_lba)/2;

	return sz;
}

char *get_ptn_size(char *buf, const char *ptn)
{
	u64 ptbl_sectors = 0;
	int i = 0, r = 0;
	u32 sz_mb;
	u64 sz = 0;

	struct ptable *gpt;
	struct mmc* mmc = NULL;

	mmc = find_mmc_device(MMC_DEVICE);
	if (mmc == NULL) {
		printf("No MMC in slot 1\n");
		return NULL;
	}
	mmc->has_init = 0;
	mmc_init(mmc);
	if (r!= 0) {
		printf("mmc init failed\n");
		return NULL;
	}

	int gpt_size = sizeof(struct ptable);

	gpt =  (struct ptable *) malloc(gpt_size);
	if (!gpt) {
		r = -1;
		goto fail;
	}

	ptbl_sectors = (u64)(gpt_size / MMCSD_SECTOR_SIZE);

	r = mmc->block_dev.block_read(1,0,ptbl_sectors,(void*)gpt);
	if (r == -1) {
		printf("error reading GPT\n");
		goto fail;
	}

	if (memcmp(gpt->header.magic, "EFI PART", 8)) {
		printf("efi partition table not found\n");
		r = -1;
		goto fail;
	}

	for (i = 0; i < EFI_ENTRIES; i++) {
		sz = get_entry_size_kb(&gpt->entry[i], ptn);
		if (sz)
			break;
	}

	if (sz >= 0xFFFFFFFF) {
		sz_mb = (u32)(sz >> 20);
		DBG("sz is > 0xFFFFFFFF\n");
		sprintf(buf, "0x%d MB", sz_mb);
	} else {
		DBG("Size of the partition = %d KB\n", (u32)sz);
		sprintf(buf, "%d KB", (u32)sz);
	}

fail:
	free((void *)gpt);
	return buf;
}

struct _partition {
	const char *name;
	unsigned size_kb;
};

/* Add this as a partition layout toggle */
static struct _partition partitions_x[] = {
 	{ "-", 128 },			//GPT
 	{ "xloader", 128 },		//p1
 	{ "bootloader", 256 },		//p2
 	{ "dkernel", 10*1024 },		//p3
 	{ "dfs", 192*1024 },		//p4
 	{ "recovery", 16*1024 },	//p5
 	{ "backup", 64*1024 },		//p6
 	{ "boot", 10*1024 },		//p7
 	{ "splash", 5*1024 },		//p8
	{ "system", 4619*1024 },	//p9
	{ "userdata", 512*1024 },	//p10
	{ "cache", 256*1024 },		//p11
	{ "media", 1536*1024 },		//p12
 	{ 0, 0 },
};

static struct _partition partitions[] = {
	{ "-", 128 },			//GPT
	{ "xloader", 128 },		//p1
	{ "bootloader", 256 },		//p2
	{ "dkernel", 10*1024 },		//p3
	{ "dfs", 192*1024 },		//p4
	{ "recovery", 16*1024 },	//p5
	{ "backup", 64*1024 },		//p6
	{ "boot", 10*1024 },		//p7
	{ "splash", 5*1024 },		//p8
	{ "system", 512*1024 },		//p9
	{ "userdata", 1137*1024 },	//p10
	{ "cache", 256*1024 },		//p11
	{ "media", 5131*1024 },		//p12
	{ 0, 0 },
};

static struct _partition *current_partitions = partitions;

#ifdef CONFIG_CMD_FASTBOOT
static int do_format(void)
{
	struct ptable *ptbl;
	unsigned blocks;
	unsigned next;
	int n;
	struct mmc* mmc = NULL;
	int status = 0;
	char *mmc_write[5]	= {"mmc", "write", NULL, NULL, NULL};

	char *dev[3] = { "mmc", "dev", "1" };
	char source[32], dest[32], length[32];

	mmc = find_mmc_device(MMC_DEVICE);
	if (mmc == NULL) {
		return -1;
	}
	mmc->has_init = 0;
	status = mmc_init(mmc);
	if (status != 0) {
		printf("mmc init failed\n");
		return status;
	}

	status = do_mmcops(NULL, 0, 3, dev);
	if (status) {
		printf("Unable to set MMC device\n");
		return status;
	}

	blocks = mmc->block_dev.lba;

	ptbl = (struct ptable *) malloc(sizeof(struct ptable));

	start_ptbl(ptbl, blocks);
	n = 0;
	next = 0;
	for (n = 0, next = 0; current_partitions[n].name; n++) {
		unsigned sz = current_partitions[n].size_kb * 2;
		if (!strcmp(current_partitions[n].name,"-")) {
			next += sz;
			continue;
		}
		if (sz == 0)
			sz = blocks - next;

	    if (add_ptn(ptbl, next, next + sz - 1, current_partitions[n].name)) {
	        printf("Add partition failed\n");
			status = -1;
	        goto fail;
		}
		next += sz;
	}
	end_ptbl(ptbl);
	fastboot_flash_reset_ptn();
	mmc_write[2] = source;
	mmc_write[3] = dest;
	mmc_write[4] = length;

	sprintf(source, "0x%x", (unsigned int)ptbl);
	sprintf(dest, "0x%x", 0x00);
	sprintf(length, "0x%x", (unsigned int)
			((sizeof(struct ptable)/mmc->block_dev.blksz) + 1));

	if (do_mmcops(NULL, 0, 5, mmc_write)) {
		printf("Writing mbr is FAILED!\n");
		goto fail;
	} else {
		printf("Writing mbr is DONE!\n");
	}

	load_ptbl();

fail:
	free(ptbl);
	return status;
}
#endif

// string_show : show string stored in the buffer
int string_show (unsigned char* buf , int num)
{
	int i = 0;
	int fine_offset = 0 ;

	if (num > CONFIG_NUM_NV_VARS) return -1;

	fine_offset = nvram_info[num].offset % MMC_BLOCK_SIZE;

	printf("%s",nvram_info[num].name);
	for (i=0; i<= (15-strlen(nvram_info[num].name)); i++){
		putc(' ');
	}
	printf("0x%04x",nvram_info[num].offset);

	for (i=1; i<=12; i++){
		putc(' ');
	}

	for (i = 0 ; i< nvram_info[num].size ; i++){

		if (*(buf+i+fine_offset) < 0x20 || *(buf+i+fine_offset) > 0x7e )
			putc('.');
		else
			printf("%c",*(buf+i+fine_offset));
	}

	printf("\n");
	return 0;
}

char *idme_cleanup(char *str)
{
    size_t len = 0;
    char *frontp = str - 1;
    char *endp = NULL;

    if( str == NULL )
            return NULL;

    if( str[0] == '\0' )
            return str;

    len = strlen(str);
    endp = str + len;

    while( !isprint(*(++frontp)) );
    while( !isprint(*(--endp)) && endp != frontp );

    if( str + len - 1 != endp )
            *(endp + 1) = '\0';
    else if( frontp != str &&  endp == frontp )
            *str = '\0';

    /* Shift the string so that it starts at str so
     * that if it's dynamically allocated, we can
     * still free it on the returned pointer.  Note
     * the reuse of endp to mean the front of the
     * string buffer now.
     */
    endp = str;
    if( frontp != str )
    {
            while( *frontp ) *endp++ = *frontp++;
            *endp = '\0';
    }


    return str;
}

#ifdef CONFIG_CMD_FASTBOOT
int do_idme(const char *cmd)
{
	unsigned char* buffer = NULL;
	int i = 0;
	int seq_no = -1;
	int rw_size, rw_page = 0;
	int rw_offset = 0;
	struct mmc* mmc = NULL;
	char *dev[3] = { "mmc", "dev", "1" };
	char *argv[CONFIG_SYS_MAXARGS + 1];	/* NULL terminated	*/
	int argc;

	if (!cmd || !*cmd)
		goto idme_cmd_usage;	/* empty command */

	/* Extract arguments */
	if ((argc = parse_line (cmd, argv)) == 0) {
		goto idme_cmd_usage;	/* empty command */
	}
	else if (argc < 2 || argc > 3 ) {
		goto idme_cmd_usage;
	}
	else {
		mmc = find_mmc_device(MMC_DEVICE);
		if (mmc == NULL) {
			printf("%s::No MMC in slot 1\n", __func__);
			return 1;
		}

		if (mmc_init(mmc)) {
			printf("%s::mmc init failed\n", __func__);
			return 1;
		}

		if (do_mmcops(NULL, 0, 3, dev)) {
			printf("Unable to set MMC device\n");
			return 1;
		}

		if (mmc_boot_part_access(mmc, 0, 0, 1)) {
			printf("%s::mmc set r/w access on boot1 failed\n", __func__);
			return 1;
		}

		//3. allocate memory and initialize to 0x20
		buffer = (unsigned char*)malloc(MMC_BLOCK_SIZE);
		memset(buffer, 0x20, MMC_BLOCK_SIZE);

		if ( argc == 2 ) { // read values
			if (strcmp(argv[1],"?") == 0){
				printf("<================== idme nvram info ==================\n");
				printf("Name		offset		  value    \n");
				printf("----------------------------------|---|---|---|---|---|\n");
				unsigned long last_sector = 99999999UL; // fake sector #
				for (i=0; i<CONFIG_NUM_NV_VARS; i++) {
					if (last_sector != (nvram_info[i].offset / mmc->read_bl_len)) {
						if (mmc->block_dev.block_read(mmc->block_dev.dev,
							(nvram_info[i].offset / mmc->read_bl_len), 1, buffer) != 0)
						last_sector = (nvram_info[i].offset / mmc->read_bl_len);
					}
					string_show(buffer, i);
				}
				printf("=================== idme nvram info ==================>\n");
			}
			else{
				free(buffer);
				goto idme_cmd_usage;
			}
		}
		else {	//argc == 3 //write
			for (i=0; i<CONFIG_NUM_NV_VARS; i++) {
				if (strcmp(argv[1],nvram_info[i].name) == 0) {
					seq_no = i;
					break;
				}
			}
			//4. check if the item is defined in the list
			if (seq_no < 0 || seq_no > CONFIG_NUM_NV_VARS) {
				printf("<idme>\"%s\" not found!\n",argv[1]);
				goto clean;
			}
			//5. check input data length
			if (strlen (argv[2]) > nvram_info[seq_no].size) {
				printf("<idme> incorrect data length, please try again.\n");
				goto clean;
			}

			rw_offset = nvram_info[seq_no].offset % mmc->read_bl_len;
			rw_page = nvram_info[seq_no].offset / mmc->read_bl_len;
			rw_size = nvram_info[seq_no].size;

			//6. read target page(512 bytes) to buffer and replace correspond bytes with desired value
			if (mmc->block_dev.block_read(mmc->block_dev.dev, rw_page, 1, buffer) == 0) {
				printf("<idme> read mmc error \n ");
				goto clean;
			}

			memset(&buffer[rw_offset], 0x20, rw_size);
			memcpy(&buffer[rw_offset], argv[2], strlen(argv[2]));

			//7. write back to EMMC page number "rw_page"
			if (mmc->block_dev.block_write(mmc->block_dev.dev, rw_page, 1, (unsigned char *)buffer) == 0){
				printf("<idme> write mmc error \n ");
				goto clean;
			}
			else{	//write success
				printf("<idme> write %s to offset 0x%04x\n",
					argv[2], nvram_info[seq_no].offset);
			}

			if (mmc_boot_part_access(mmc, 0, 0, 0)) {
				printf("%s::mmc set readonly access on boot1 failed\n", __func__);
			}

		}
		free(buffer);
	}

	return 0;

idme_cmd_usage:
	printf("Usage:\n"
		"idme    - idme control interface\n"
		"idme <var> <value> --- set a variable value \n"
		"idme ?             --- print out known values\n");
	return 1;

clean:
	mmc_boot_part_access(mmc, 0, 0, 0);
	if (buffer) free(buffer);
	return 1;
}

int do_usbboot(const char *cmd)
{
	__raw_writel( 0x4A326A0C , 0x4A326A00 );   // Address in Public Internal SRAM where SW Booting Configuration is
	__raw_writel( 0xCF00AA01 , 0x4A326A0C );   // Header for SW Booting Configuration
	__raw_writel( 0x0000000C , 0x4A326A10 );
	__raw_writel( 0x00450000 , 0x4A326A14 );   // USB Boot First
	__raw_writel( 0x00000000 , 0x4A326A18 );
	__raw_writel( 0x00000000 , 0x4A326A1C );
	/* now warm reset the silicon */
	__raw_writel( (1<<0) , PRM_RSTCTRL);

	return 0; // never gets here
}

int do_kc1_usbboot(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return do_usbboot(NULL);
}

int do_bootsize_change(struct mmc* mmc, unsigned int bootpart, unsigned int bootpart_size)
{
	char bootpartsize_cmd[250];

	sprintf(bootpartsize_cmd, "mmc bootpart 1 %u %u", bootpart_size, bootpart_size);

	if (mmc_boot_part_access(mmc, 0, 0, bootpart+1)) {
		lcd_printf("ERROR: Unable to open boot%d!\n", bootpart);
		return 1;
	}

	if (run_command(bootpartsize_cmd, 0)) {
		lcd_printf("ERROR: setting bootsize.\n");
	}

	if (mmc_boot_part_access(mmc, 0, 0, 0)) {
		lcd_printf("ERROR: Unable to close boot%d!\n", bootpart);
		return 1;
	}

	return 0;
}

void * do_backup_partition(struct mmc* mmc, char *part_name)
{
	void *backup_data = NULL;
	unsigned long backup_size = 0;
	struct fastboot_ptentry *ptn;

	if (!mmc) {
		lcd_printf("ERROR: MMC uninitialized!\n");
		return NULL;
	}

	ptn = fastboot_flash_find_ptn(part_name);
	if (!ptn) {
		lcd_printf("ERROR: partition '%s' not found!\n", part_name);
		return NULL;
	}

	backup_size = ((ptn->length + (mmc->write_bl_len - 1)) / mmc->write_bl_len) * mmc->write_bl_len;
	backup_data = malloc(backup_size);
	if (!backup_data) {
		lcd_printf("ERROR: No backup data found for '%s'!\n", part_name);
		return NULL;
	}

	// backup early partitions
	if (mmc->block_dev.block_read(mmc->block_dev.dev,
			ptn->start,
			((ptn->length + (mmc->write_bl_len - 1)) / mmc->write_bl_len),
			backup_data) == 0)
	{
		free(backup_data);
		backup_data = NULL;
	}

	return backup_data;
}

int do_restore_partition(struct mmc* mmc, char *part_name, void *backup_data)
{
	int status = 1;
	struct fastboot_ptentry *ptn;

	if (!mmc) {
		lcd_printf("ERROR: MMC uninitialized!\n");
		return 1;
	}

	ptn = fastboot_flash_find_ptn(part_name);
	if (!ptn) {
		lcd_printf("ERROR: partition '%s' not found!\n", part_name);
		return 1;
	}

	if (!backup_data) {
		lcd_printf("ERROR: No backup data found for '%s'!\n", part_name);
		return 1;
	}


	if (mmc->block_dev.block_write(mmc->block_dev.dev, ptn->start,
			(ptn->length + (mmc->write_bl_len - 1)) / mmc->write_bl_len,
			backup_data) != 0)
		status = 0;

	free(backup_data);
	return status;
}

int do_emmcfix(const char *cmd)
{
	void *backup_xloader = NULL;
	void *backup_bootloader = NULL;
	struct mmc* mmc = NULL;
	char *dev[3] = { "mmc", "dev", "1" };
	int ret = 1;

	show_fastbootmode();

	lcd_printf("Starting EMMC corruption wipe process.\n");

	mmc = find_mmc_device(MMC_DEVICE);
	if (mmc == NULL) {
		lcd_printf("ERROR: No MMC in slot 1!  ABORT.\n");
		goto cleanup;
	}

	if (mmc_init(mmc)) {
		lcd_printf("ERROR: MMC init failed!  ABORT.\n");
		goto cleanup;
	}

	if (do_mmcops(NULL, 0, 3, dev)) {
		lcd_printf("ERROR: Unable to set MMC device!  ABORT.\n");
		goto cleanup;
	}

	// backup early partitions

	lcd_printf("Backing up xloader ...");
	backup_xloader = do_backup_partition(mmc, "xloader");
	if (!backup_xloader) {
		lcd_printf("FAILED! ABORT!\n");
		goto cleanup;
	}
	else
		lcd_printf("Done!\n");

	lcd_printf("Backing up bootloader ...");
	backup_bootloader = do_backup_partition(mmc, "bootloader");
	if (!backup_bootloader) {
		lcd_printf("FAILED! ABORT!\n");
		goto cleanup;
	}
	else
		lcd_printf("Done!\n");

	// reset bootsize

	lcd_printf("Clearing EMMC chip: stage 1 ...");
	ret = do_bootsize_change(mmc, 0, 0);
	if (ret) {
		lcd_printf("ERROR: Writing bootsize 0 to boot0!\n");
	}
	/* Waiting for the ready status */
	udelay(10000000);
	ret = do_bootsize_change(mmc, 1, 0);
	/* Waiting for the ready status */
	udelay(10000000);
	if (ret) {
		lcd_printf("ERROR: Writing bootsize 0 to boot1!\n");
	}
	else {
		lcd_printf(" Done!\n");
	}

	lcd_printf("Clearing EMMC chip: stage 2 ...");
	ret = do_bootsize_change(mmc, 0, 512);
	if (ret) {
		lcd_printf("ERROR: Writing bootsize 512kb to boot0!\n");
	}
	/* Waiting for the ready status */
	udelay(10000000);
	ret = do_bootsize_change(mmc, 1, 512);
	/* Waiting for the ready status */
	udelay(10000000);
	if (ret) {
		lcd_printf("ERROR: Writing bootsize 512kb to boot1!\n");
	}
	else {
		lcd_printf(" Done!\n");
	}

	if (do_mmcops(NULL, 0, 3, dev)) {
		lcd_printf(" ERROR: Resetting MMC device!\n");
	}
	else {
		lcd_printf(" Done!\n");
	}

	// Write partition layout

	lcd_printf("Writing partition layout ...");
	if (do_format()) {
		lcd_printf("ERROR!? CONTINUING.. EMMC IS BLANK AT THIS POINT.\n");
	}
	else
		lcd_printf("Done!\n");

	udelay(1000000); // pause 1 sec

	// restore boot parts

	lcd_printf("Restoring 'xloader' ...");
	if (do_restore_partition(mmc, "xloader", backup_xloader))
		lcd_printf("FAILED! Need to re-flash manually!\n");
	else
		lcd_printf("Done!\n");
	backup_xloader = NULL;

	lcd_printf("Restoring 'bootloader' ...");
	if (do_restore_partition(mmc, "bootloader", backup_bootloader))
		lcd_printf("FAILED! Need to re-flash manually!\n");
	else
		lcd_printf("Done!\n");
	backup_bootloader = NULL;

	lcd_printf("EMMC corruption fix complete.  Use fastboot to flash recovery and then reboot.\n");
	ret = 0;

cleanup:
	if (backup_xloader)
		free(backup_xloader);
	if (backup_bootloader)
		free(backup_bootloader);
	return ret;
}

int do_kc1_emmcfix(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return do_emmcfix(NULL);
}

int do_bootsize(const char *cmd)
{
	struct mmc* mmc = NULL;
	char *argv[CONFIG_SYS_MAXARGS + 1];	/* NULL terminated	*/
	int argc;
	char *dev[3] = { "mmc", "dev", "1" };
	unsigned int bootpart = 0;
	unsigned int bootpartsize = 0;

	if (!cmd || !*cmd)
		goto bootsize_cmd_usage;	/* empty command */

	/* Extract arguments */
	if ((argc = parse_line (cmd, argv)) == 0) {
		goto bootsize_cmd_usage;	/* empty command */
	}
	else if (argc != 3) {
		goto bootsize_cmd_usage;
	}
	else {
		bootpart = (unsigned int)simple_strtoul(argv[1], NULL, 10);
		bootpartsize = (unsigned int)simple_strtoul(argv[2], NULL, 10);

		mmc = find_mmc_device(MMC_DEVICE);
		if (mmc == NULL) {
			lcd_printf("ERROR: No MMC in slot 1!  ABORT.\n");
			return 1;
		}

		if (mmc_init(mmc)) {
			lcd_printf("ERROR: MMC init failed!  ABORT.\n");
			return 1;
		}

		if (do_mmcops(NULL, 0, 3, dev)) {
			printf("Unable to set MMC device\n");
			return 1;
		}

		return do_bootsize_change(mmc, bootpart, bootpartsize);
	}

bootsize_cmd_usage:
	printf("Usage:\n"
		"bootsize - change boot partition size\n");
	return 1;
}

int do_swap_partition_mode(const char *cmd)
{
	if (current_partitions == partitions_x)
		current_partitions = partitions;
	else
		current_partitions = partitions_x;
	return do_format();
}

int fastboot_oem(const char *cmd)
{
	if (memcmp(cmd, "format", 6) == 0) {
		return do_format();
	}
	if (memcmp(cmd, "idme", 4) == 0) {
		return do_idme(cmd);
	}
	if (memcmp(cmd, "usbboot", 7) == 0) {
		return do_usbboot(cmd);
	}
	if (memcmp(cmd, "emmcfix", 7) == 0) {
		return do_emmcfix(cmd);
	}
	if (memcmp(cmd, "swap_partition_mode", 19) == 0) {
		return do_swap_partition_mode(cmd);
	}
	if (memcmp(cmd, "bootsize", 8) == 0) {
		return do_bootsize(cmd);
	}
	return -1;
}
#endif

int idme_loadvalue(int index, char *buffer, int length) {
	char *load_buffer = NULL;
	int offset;
	char *dev[3] = { "mmc", "dev", "1" };
	struct mmc* mmc = NULL;
	int r = 0;

	mmc = find_mmc_device(MMC_DEVICE);
	if (mmc == NULL) {
		printf("%s::No MMC in slot 1\n", __func__);
		return -1;
	}
	r = mmc_init(mmc);
	if (r != 0) {
		printf("%s::mmc init failed\n", __func__);
		return r;
	}

	if (do_mmcops(NULL, 0, 3, dev)) {
		printf("Unable to set MMC device\n");
		return 1;
	}

	r = mmc_boot_part_access(mmc, 0, 0, 1);
	if (r != 0) {
		printf("%s::mmc boot access 1 failed (%d)\n", __func__, r);
		return r;
	}
	load_buffer = (char *)malloc(MMC_BLOCK_SIZE);
	r = mmc->block_dev.block_read(MMC_DEVICE, (nvram_info[index].offset / mmc->read_bl_len), 1, load_buffer);
	mmc_boot_part_access(mmc, 0, 0, 0);
	if (r != 1) {
		printf("%s::block_read failed (blkcount=%d)\n", __func__, r);
		return -1;
	}
	offset = nvram_info[index].offset % MMC_BLOCK_SIZE;

	memcpy(buffer, (load_buffer + offset), length);
	free(load_buffer);
	return 0;
}

int board_mmc_ftbtptn_init(void)
{
	char buffer[100];
	int ret = 0;

	// Load Serial
	if (!idme_loadvalue(0, buffer, 20)) {
		strncpy(idme_serial, buffer, 16);
		idme_serial[16]='\0';
		idme_cleanup(idme_serial);
	}
	else {
		printf("idme load serial failed\n");
	}

	// Load MAC
	if (!idme_loadvalue(1, buffer, 20)) {
		strncpy(idme_macaddr, buffer, 12);
		idme_macaddr[12]='\0';
		idme_cleanup(idme_macaddr);
	}
	else {
		printf("idme load macaddr failed\n");
	}

	// Load Settings
	if (!idme_loadvalue(10, buffer, 2)) {
		idme_settings = (unsigned int)(buffer[0]-48);
	}

	setenv("idme_serialno", idme_serial);
	setenv("idme_wifimac", idme_macaddr);
	setenv_hex("idme_settings", idme_settings);

	printf("\nefi partition table:\n");
	ret = load_ptbl();

	// Load partition mode
	if (fastboot_flash_find_ptn("spacer"))
		current_partitions = partitions_x;
	else
		current_partitions = partitions;

	return ret;
}

char* fastboot_get_serialno(void)
{
	return idme_serial;
}

char* fastboot_get_macaddr(void)
{
	return idme_macaddr;
}

int fastboot_write_settings(unsigned int new_settings)
{
	char buffer[25];
	idme_settings = new_settings;
	setenv_hex("idme_settings", idme_settings);
	sprintf(buffer, "idme settings %2x", idme_settings);
	return do_idme(buffer);
}

unsigned long fastboot_get_settings(void)
{
	return idme_settings;
}

int fastboot_get_setting_bit(unsigned char bit)
{
	return ((idme_settings & (1<<bit)) != 0);
}

void fastboot_set_setting_bit(unsigned char bit, unsigned char value)
{
	if (value)
		idme_settings |= (1<<bit);
	else
		idme_settings &= ~(1<<bit);
}

void kc1_findreplace(char *text_to_find, char *text_to_replace, char *buffer)
{
	char temp[255];

	char *pos = strstr(buffer, text_to_find);
	if (pos != NULL)
	{
		/* Copy the text before the text to replace */
		memcpy(temp, buffer, pos - buffer);

		/* Copy in the replacement text */
		memcpy(temp + (pos - buffer), text_to_replace, strlen(text_to_replace));

		/* Copy the remaining text from after the replace text */
		memcpy(temp + (pos - buffer) + strlen(text_to_replace),
			pos + strlen(text_to_find),
			1 + strlen(buffer) - ((pos - buffer) + strlen(text_to_find)));

		strcpy(buffer, temp);
	}
}

void lcdmenu_processvars(char *buffer)
{
// [0] CHARGER MODE:    #charger_mode_flag#     \e[32mON\e[37m
// [1] SERIAL CONSOLE:  #serial_console_flag#   \e[37mOFF\e[37m
// ___ SERIAL #:        #serial_no#             \e[36m0123456789012345\e[37m
// ___ WIFI MAC ADDR:   #wifi_mac#              \e[36m00:00:00:00:00:00\e[37m
// [2] PARTITION MODE:  #partition_mode#        \e[36mOTTERX\e[37m
	char temp[200];
	if (fastboot_get_setting_bit(0)) {
		kc1_findreplace("#charger_mode_flag#", "\e[32mON\e[37m", buffer);
		kc1_findreplace("#charger_mode_menu_id#", "2", buffer);
	}
	else {
		kc1_findreplace("#charger_mode_flag#", "\e[37mOFF\e[37m", buffer);
		kc1_findreplace("#charger_mode_menu_id#", "3", buffer);
	}
	if (fastboot_get_setting_bit(1)) {
		kc1_findreplace("#serial_console_flag#", "\e[32mON\e[37m", buffer);
		kc1_findreplace("#serial_console_menu_id#", "4", buffer);
	}
	else {
		kc1_findreplace("#serial_console_flag#", "\e[37mOFF\e[37m", buffer);
		kc1_findreplace("#serial_console_menu_id#", "5", buffer);
	}
	sprintf(temp, "\e[36m%s\e[37m", fastboot_get_serialno());
	kc1_findreplace("#serial_no#", temp, buffer);
	sprintf(temp, "\e[36m%s\e[37m", fastboot_get_macaddr());
	kc1_findreplace("#wifi_mac#", temp, buffer);
	if (current_partitions == partitions_x) {
		kc1_findreplace("#partition_mode#", "\e[36mOTTERX\e[37m", buffer);
		kc1_findreplace("#partition_mode_other#", "\e[36mAMAZON\e[37m", buffer);
	}
	else {
		kc1_findreplace("#partition_mode#", "\e[36mAMAZON\e[37m", buffer);
		kc1_findreplace("#partition_mode_other#", "\e[36mOTTERX\e[37m", buffer);
	}
}


int do_idme_settings(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	unsigned char bit = 0;
	unsigned char value = 0;

	if (argc != 3)
		return 1;

	bit = (unsigned char)simple_strtoul(argv[1], NULL, 16);
	value = (unsigned char)simple_strtoul(argv[2], NULL, 16);
	fastboot_set_setting_bit(bit, value);

	return fastboot_write_settings(fastboot_get_settings());
}

int do_kc1_swap_partition_mode(cmd_tbl_t *cmdtp, int flag, int argc, char * const argv[])
{
	return do_swap_partition_mode(NULL);
}

U_BOOT_CMD( idme_settings, 3, 1, do_idme_settings, "idme_settings <bit> <value>", NULL );
U_BOOT_CMD( kc1_swap_partition_mode, 1, 1, do_kc1_swap_partition_mode, "kc1_swap_partition_mode", NULL );
U_BOOT_CMD( kc1_usbboot, 1, 1, do_kc1_usbboot, "kc1_usbboot", NULL );
U_BOOT_CMD( kc1_emmcfix, 1, 1, do_kc1_emmcfix, "kc1_emmcfix", NULL );
