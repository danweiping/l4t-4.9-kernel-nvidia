/*
 * drivers/video/tegra/host/dev.h
 *
 * Copyright (c) 2012, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef NVHOST_DEV_H
#define NVHOST_DEV_H

#include "host1x/host1x.h"

struct platform_device;

void nvhost_device_list_init(void);
int nvhost_device_list_add(struct platform_device *pdev);
void nvhost_device_list_for_all(void *data,
	int (*fptr)(struct platform_device *pdev, void *fdata));
struct platform_device *nvhost_device_list_match_by_id(u32 id);
void nvhost_device_list_remove(struct platform_device *pdev);


/* debug info */
/*#define NVHOST_DEBUG*/
#define NVHOST_DBG_MASK (dbg_info|dbg_fn)

enum nvhost_dbg_categories {
	dbg_info    = BIT(0),  /* lightly verbose info */
	dbg_err     = BIT(1),  /* verbosity around errors*/
	dbg_fn      = BIT(2),  /* fn name tracing */
	dbg_reg     = BIT(3),  /* register accesses, very verbose */
	dbg_pte     = BIT(4),  /* gmmu ptes */
	dbg_intr    = BIT(5),  /* interrupts */
	dbg_pmu     = BIT(6),  /* gk20a pmu */
	dbg_clk     = BIT(7),  /* gk20a clk */
	dbg_mem     = BIT(31), /* memory accesses, very verbose */
};

#if defined(NVHOST_DEBUG)
extern u32 nvhost_dbg_mask;
#define nvhost_dbg(dbg_mask, format, arg...)				\
do {									\
	if ((dbg_mask) & nvhost_dbg_mask)				\
		printk(KERN_DEBUG "nvhost %s: " format "\n", __func__, ##arg);\
} while (0)

#else /* NVHOST_DEBUG */
#define nvhost_dbg(dbg_mask, format, arg...)				\
do {									\
	if (0)								\
		printk(KERN_DEBUG "nvhost %s: " format "\n", __func__, ##arg);\
} while (0)

#endif


/* convenience,shorter err/fn/dbg_info */
#define nvhost_err(d, fmt, arg...) \
	dev_err(d, "%s: " fmt "\n", __func__, ##arg)

#define nvhost_warn(d, fmt, arg...) \
	dev_warn(d, "%s: " fmt "\n", __func__, ##arg)

#define nvhost_dbg_fn(fmt, arg...) \
	nvhost_dbg(dbg_fn, fmt, ##arg)

#define nvhost_dbg_info(fmt, arg...) \
	nvhost_dbg(dbg_info, fmt, ##arg)

/* mem access with dbg_mem logging */
static inline u8 mem_rd08(void *ptr, int b)
{
	u8 _b = ((const u8 *)ptr)[b];
	nvhost_dbg(dbg_mem, " %p = 0x%x", ptr+sizeof(u8)*b, _b);
	return _b;
}
static inline u16 mem_rd16(void *ptr, int s)
{
	u16 _s = ((const u16 *)ptr)[s];
	nvhost_dbg(dbg_mem, " %p = 0x%x", ptr+sizeof(u16)*s, _s);
	return _s;
}
static inline u32 mem_rd32(void *ptr, int w)
{
	u32 _w = ((const u32 *)ptr)[w];
	nvhost_dbg(dbg_mem, " %p = 0x%x", ptr + sizeof(u32)*w, _w);
	return _w;
}
static inline void mem_wr08(void *ptr, int b, u8 data)
{
	nvhost_dbg(dbg_mem, " %p = 0x%x", ptr+sizeof(u8)*b, data);
	((u8 *)ptr)[b] = data;
}
static inline void mem_wr16(void *ptr, int s, u16 data)
{
	nvhost_dbg(dbg_mem, " %p = 0x%x", ptr+sizeof(u16)*s, data);
	((u16 *)ptr)[s] = data;
}
static inline void mem_wr32(void *ptr, int w, u32 data)
{
	nvhost_dbg(dbg_mem, " %p = 0x%x", ptr+sizeof(u32)*w, data);
	((u32 *)ptr)[w] = data;
}

#endif
