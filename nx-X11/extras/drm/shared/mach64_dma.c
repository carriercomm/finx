/* mach64_dma.c -- DMA support for mach64 (Rage Pro) driver -*- linux-c -*-
 * Created: Sun Dec 03 19:20:26 2000 by gareth@valinux.com
 *
 * Copyright 2000 Gareth Hughes
 * Copyright 2002 Frank C. Earl
 * Copyright 2002-2003 Leif Delgass
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
 * THE COPYRIGHT OWNER(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER
 * IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors:
 *   Gareth Hughes <gareth@valinux.com>
 *   Frank C. Earl <fearl@airmail.net>
 *   Leif Delgass <ldelgass@retinalburn.net>
 *   Jos� Fonseca <j_r_fonseca@yahoo.co.uk>
 */

#include "mach64.h"
#include "drmP.h"
#include "drm.h"
#include "mach64_drm.h"
#include "mach64_drv.h"


/* ================================================================
 * Engine, FIFO control
 */

int mach64_do_wait_for_fifo( drm_mach64_private_t *dev_priv, int entries )
{
	int slots = 0, i;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		slots = (MACH64_READ( MACH64_FIFO_STAT ) &
			 MACH64_FIFO_SLOT_MASK);
		if ( slots <= (0x8000 >> entries) ) return 0;
		DRM_UDELAY( 1 );
	}

	DRM_INFO( "%s failed! slots=%d entries=%d\n", __FUNCTION__, slots, entries );
	return DRM_ERR(EBUSY);
}

int mach64_do_wait_for_idle( drm_mach64_private_t *dev_priv )
{
	int i, ret;

	ret = mach64_do_wait_for_fifo( dev_priv, 16 );
	if ( ret < 0 ) return ret;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		if ( !(MACH64_READ( MACH64_GUI_STAT ) & MACH64_GUI_ACTIVE) ) {
			return 0;
		}
		DRM_UDELAY( 1 );
	}

	DRM_INFO( "%s failed! GUI_STAT=0x%08x\n", __FUNCTION__, 
		   MACH64_READ( MACH64_GUI_STAT ) );
	mach64_dump_ring_info( dev_priv );
	return DRM_ERR(EBUSY);
}

int mach64_wait_ring( drm_mach64_private_t *dev_priv, int n )
{
	drm_mach64_descriptor_ring_t *ring = &dev_priv->ring;
	int i;

	for ( i = 0 ; i < dev_priv->usec_timeout ; i++ ) {
		mach64_update_ring_snapshot( dev_priv );
		if ( ring->space >= n ) {
			if (i > 0) {
				DRM_DEBUG( "%s: %d usecs\n", __FUNCTION__, i );
			}
			return 0;
		}
		DRM_UDELAY( 1 );
	}

	/* FIXME: This is being ignored... */
	DRM_ERROR( "failed!\n" );
	mach64_dump_ring_info( dev_priv );
	return DRM_ERR(EBUSY);
}

/* Wait until all DMA requests have been processed... */
static int mach64_ring_idle( drm_mach64_private_t *dev_priv )
{
	drm_mach64_descriptor_ring_t *ring = &dev_priv->ring;
	u32 head;
	int i;

	head = ring->head;
	i = 0;
	while ( i < dev_priv->usec_timeout ) {
		mach64_update_ring_snapshot( dev_priv );
		if ( ring->head == ring->tail && 
		     !(MACH64_READ(MACH64_GUI_STAT) & MACH64_GUI_ACTIVE) ) {
			if (i > 0) {
				DRM_DEBUG( "%s: %d usecs\n", __FUNCTION__, i );
			}
			return 0;
		} 
		if ( ring->head == head ) {
			++i;
		} else {
			head = ring->head;
			i = 0;
		}
		DRM_UDELAY( 1 );
	}

	DRM_INFO( "%s failed! GUI_STAT=0x%08x\n", __FUNCTION__, 
		   MACH64_READ( MACH64_GUI_STAT ) );
	mach64_dump_ring_info( dev_priv );
	return DRM_ERR(EBUSY);
}

static void mach64_ring_reset( drm_mach64_private_t *dev_priv )
{
	drm_mach64_descriptor_ring_t *ring = &dev_priv->ring;

	mach64_do_release_used_buffers( dev_priv );
	ring->head_addr = ring->start_addr;
	ring->head = ring->tail = 0;
	ring->space = ring->size;
	
	MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD, 
		      ring->head_addr | MACH64_CIRCULAR_BUF_SIZE_16KB );

	dev_priv->ring_running = 0;
}

int mach64_do_dma_flush( drm_mach64_private_t *dev_priv )
{
	/* FIXME: It's not necessary to wait for idle when flushing 
	 * we just need to ensure the ring will be completely processed
	 * in finite time without another ioctl
	 */
	return mach64_ring_idle( dev_priv );
}

int mach64_do_dma_idle( drm_mach64_private_t *dev_priv )
{
	int ret;

	/* wait for completion */
	if ( (ret = mach64_ring_idle( dev_priv )) < 0 ) {
		DRM_ERROR( "%s failed BM_GUI_TABLE=0x%08x tail: %u\n", __FUNCTION__, 
			   MACH64_READ(MACH64_BM_GUI_TABLE), dev_priv->ring.tail );
		return ret;
	}

	mach64_ring_stop( dev_priv );

	/* clean up after pass */
	mach64_do_release_used_buffers( dev_priv );
	return 0;
}

/* Reset the engine.  This will stop the DMA if it is running.
 */
int mach64_do_engine_reset( drm_mach64_private_t *dev_priv )
{
	u32 tmp;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	/* Kill off any outstanding DMA transfers.
	 */
	tmp = MACH64_READ( MACH64_BUS_CNTL );
	MACH64_WRITE( MACH64_BUS_CNTL,
				  tmp | MACH64_BUS_MASTER_DIS );
	
	/* Reset the GUI engine (high to low transition).
	 */
	tmp = MACH64_READ( MACH64_GEN_TEST_CNTL );
	MACH64_WRITE( MACH64_GEN_TEST_CNTL,
				  tmp & ~MACH64_GUI_ENGINE_ENABLE );
	/* Enable the GUI engine
	 */
	tmp = MACH64_READ( MACH64_GEN_TEST_CNTL );
	MACH64_WRITE( MACH64_GEN_TEST_CNTL,
				  tmp | MACH64_GUI_ENGINE_ENABLE );

	/* ensure engine is not locked up by clearing any FIFO or HOST errors
	*/
	tmp = MACH64_READ( MACH64_BUS_CNTL );
	MACH64_WRITE( MACH64_BUS_CNTL, tmp | 0x00a00000 );

	/* Once GUI engine is restored, disable bus mastering */
	MACH64_WRITE( MACH64_SRC_CNTL, 0 );

	/* Reset descriptor ring */
	mach64_ring_reset( dev_priv );
	
	return 0;
}

/* ================================================================
 * Debugging output
 */

void mach64_dump_engine_info( drm_mach64_private_t *dev_priv )
{
	DRM_INFO( "\n" );
	if ( !dev_priv->is_pci )
	{
		DRM_INFO( "           AGP_BASE = 0x%08x\n", MACH64_READ( MACH64_AGP_BASE ) );
		DRM_INFO( "           AGP_CNTL = 0x%08x\n", MACH64_READ( MACH64_AGP_CNTL ) );
	}
	DRM_INFO( "     ALPHA_TST_CNTL = 0x%08x\n", MACH64_READ( MACH64_ALPHA_TST_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "         BM_COMMAND = 0x%08x\n", MACH64_READ( MACH64_BM_COMMAND ) );
	DRM_INFO( "BM_FRAME_BUF_OFFSET = 0x%08x\n", MACH64_READ( MACH64_BM_FRAME_BUF_OFFSET ) );
	DRM_INFO( "       BM_GUI_TABLE = 0x%08x\n", MACH64_READ( MACH64_BM_GUI_TABLE ) );
	DRM_INFO( "          BM_STATUS = 0x%08x\n", MACH64_READ( MACH64_BM_STATUS ) );
	DRM_INFO( " BM_SYSTEM_MEM_ADDR = 0x%08x\n", MACH64_READ( MACH64_BM_SYSTEM_MEM_ADDR ) );
	DRM_INFO( "    BM_SYSTEM_TABLE = 0x%08x\n", MACH64_READ( MACH64_BM_SYSTEM_TABLE ) );
	DRM_INFO( "           BUS_CNTL = 0x%08x\n", MACH64_READ( MACH64_BUS_CNTL ) );
	DRM_INFO( "\n" );
	/* DRM_INFO( "         CLOCK_CNTL = 0x%08x\n", MACH64_READ( MACH64_CLOCK_CNTL ) ); */
	DRM_INFO( "        CLR_CMP_CLR = 0x%08x\n", MACH64_READ( MACH64_CLR_CMP_CLR ) );
	DRM_INFO( "       CLR_CMP_CNTL = 0x%08x\n", MACH64_READ( MACH64_CLR_CMP_CNTL ) );
	/* DRM_INFO( "        CLR_CMP_MSK = 0x%08x\n", MACH64_READ( MACH64_CLR_CMP_MSK ) ); */
	DRM_INFO( "     CONFIG_CHIP_ID = 0x%08x\n", MACH64_READ( MACH64_CONFIG_CHIP_ID ) );
	DRM_INFO( "        CONFIG_CNTL = 0x%08x\n", MACH64_READ( MACH64_CONFIG_CNTL ) );
	DRM_INFO( "       CONFIG_STAT0 = 0x%08x\n", MACH64_READ( MACH64_CONFIG_STAT0 ) );
	DRM_INFO( "       CONFIG_STAT1 = 0x%08x\n", MACH64_READ( MACH64_CONFIG_STAT1 ) );
	DRM_INFO( "       CONFIG_STAT2 = 0x%08x\n", MACH64_READ( MACH64_CONFIG_STAT2 ) );
	DRM_INFO( "            CRC_SIG = 0x%08x\n", MACH64_READ( MACH64_CRC_SIG ) );
	DRM_INFO( "  CUSTOM_MACRO_CNTL = 0x%08x\n", MACH64_READ( MACH64_CUSTOM_MACRO_CNTL ) );
	DRM_INFO( "\n" );
	/* DRM_INFO( "           DAC_CNTL = 0x%08x\n", MACH64_READ( MACH64_DAC_CNTL ) ); */
	/* DRM_INFO( "           DAC_REGS = 0x%08x\n", MACH64_READ( MACH64_DAC_REGS ) ); */
	DRM_INFO( "        DP_BKGD_CLR = 0x%08x\n", MACH64_READ( MACH64_DP_BKGD_CLR ) );
	DRM_INFO( "        DP_FRGD_CLR = 0x%08x\n", MACH64_READ( MACH64_DP_FRGD_CLR ) );
	DRM_INFO( "             DP_MIX = 0x%08x\n", MACH64_READ( MACH64_DP_MIX ) );
	DRM_INFO( "       DP_PIX_WIDTH = 0x%08x\n", MACH64_READ( MACH64_DP_PIX_WIDTH ) );
	DRM_INFO( "             DP_SRC = 0x%08x\n", MACH64_READ( MACH64_DP_SRC ) );
	DRM_INFO( "      DP_WRITE_MASK = 0x%08x\n", MACH64_READ( MACH64_DP_WRITE_MASK ) );
	DRM_INFO( "         DSP_CONFIG = 0x%08x\n", MACH64_READ( MACH64_DSP_CONFIG ) );
	DRM_INFO( "         DSP_ON_OFF = 0x%08x\n", MACH64_READ( MACH64_DSP_ON_OFF ) );
	DRM_INFO( "           DST_CNTL = 0x%08x\n", MACH64_READ( MACH64_DST_CNTL ) );
	DRM_INFO( "      DST_OFF_PITCH = 0x%08x\n", MACH64_READ( MACH64_DST_OFF_PITCH ) );
	DRM_INFO( "\n" );
	/* DRM_INFO( "       EXT_DAC_REGS = 0x%08x\n", MACH64_READ( MACH64_EXT_DAC_REGS ) ); */
	DRM_INFO( "       EXT_MEM_CNTL = 0x%08x\n", MACH64_READ( MACH64_EXT_MEM_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "          FIFO_STAT = 0x%08x\n", MACH64_READ( MACH64_FIFO_STAT ) );
	DRM_INFO( "\n" );
	DRM_INFO( "      GEN_TEST_CNTL = 0x%08x\n", MACH64_READ( MACH64_GEN_TEST_CNTL ) );
	/* DRM_INFO( "              GP_IO = 0x%08x\n", MACH64_READ( MACH64_GP_IO ) ); */
	DRM_INFO( "   GUI_CMDFIFO_DATA = 0x%08x\n", MACH64_READ( MACH64_GUI_CMDFIFO_DATA ) );
	DRM_INFO( "  GUI_CMDFIFO_DEBUG = 0x%08x\n", MACH64_READ( MACH64_GUI_CMDFIFO_DEBUG ) );
	DRM_INFO( "           GUI_CNTL = 0x%08x\n", MACH64_READ( MACH64_GUI_CNTL ) );
	DRM_INFO( "           GUI_STAT = 0x%08x\n", MACH64_READ( MACH64_GUI_STAT ) );
	DRM_INFO( "      GUI_TRAJ_CNTL = 0x%08x\n", MACH64_READ( MACH64_GUI_TRAJ_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "          HOST_CNTL = 0x%08x\n", MACH64_READ( MACH64_HOST_CNTL ) );
	DRM_INFO( "           HW_DEBUG = 0x%08x\n", MACH64_READ( MACH64_HW_DEBUG ) );
	DRM_INFO( "\n" );
	DRM_INFO( "    MEM_ADDR_CONFIG = 0x%08x\n", MACH64_READ( MACH64_MEM_ADDR_CONFIG ) );
	DRM_INFO( "       MEM_BUF_CNTL = 0x%08x\n", MACH64_READ( MACH64_MEM_BUF_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "           PAT_REG0 = 0x%08x\n", MACH64_READ( MACH64_PAT_REG0 ) );
	DRM_INFO( "           PAT_REG1 = 0x%08x\n", MACH64_READ( MACH64_PAT_REG1 ) );
	DRM_INFO( "\n" );
	DRM_INFO( "            SC_LEFT = 0x%08x\n", MACH64_READ( MACH64_SC_LEFT ) );
	DRM_INFO( "           SC_RIGHT = 0x%08x\n", MACH64_READ( MACH64_SC_RIGHT ) );
	DRM_INFO( "             SC_TOP = 0x%08x\n", MACH64_READ( MACH64_SC_TOP ) );
	DRM_INFO( "          SC_BOTTOM = 0x%08x\n", MACH64_READ( MACH64_SC_BOTTOM ) );
	DRM_INFO( "\n" );
	DRM_INFO( "      SCALE_3D_CNTL = 0x%08x\n", MACH64_READ( MACH64_SCALE_3D_CNTL ) );
	DRM_INFO( "       SCRATCH_REG0 = 0x%08x\n", MACH64_READ( MACH64_SCRATCH_REG0 ) );
	DRM_INFO( "       SCRATCH_REG1 = 0x%08x\n", MACH64_READ( MACH64_SCRATCH_REG1 ) );
	DRM_INFO( "         SETUP_CNTL = 0x%08x\n", MACH64_READ( MACH64_SETUP_CNTL ) );
	DRM_INFO( "           SRC_CNTL = 0x%08x\n", MACH64_READ( MACH64_SRC_CNTL ) );
	DRM_INFO( "\n" );
	DRM_INFO( "           TEX_CNTL = 0x%08x\n", MACH64_READ( MACH64_TEX_CNTL ) );
	DRM_INFO( "     TEX_SIZE_PITCH = 0x%08x\n", MACH64_READ( MACH64_TEX_SIZE_PITCH ) );
	DRM_INFO( "       TIMER_CONFIG = 0x%08x\n", MACH64_READ( MACH64_TIMER_CONFIG ) );
	DRM_INFO( "\n" );
	DRM_INFO( "             Z_CNTL = 0x%08x\n", MACH64_READ( MACH64_Z_CNTL ) );
	DRM_INFO( "        Z_OFF_PITCH = 0x%08x\n", MACH64_READ( MACH64_Z_OFF_PITCH ) );
	DRM_INFO( "\n" );
}

#define MACH64_DUMP_CONTEXT	3

static void mach64_dump_buf_info( drm_mach64_private_t *dev_priv, drm_buf_t *buf)
{
	u32 addr = GETBUFADDR( buf );
	u32 used = buf->used >> 2;
	u32 sys_addr = MACH64_READ( MACH64_BM_SYSTEM_MEM_ADDR );
	u32 *p = GETBUFPTR( buf );
	int skipped = 0;

	DRM_INFO( "buffer contents:\n" );
	
	while ( used ) {
		u32 reg, count;

		reg = le32_to_cpu(*p++);
		if( addr <= GETBUFADDR( buf ) + MACH64_DUMP_CONTEXT * 4 ||
		    (addr >= sys_addr - MACH64_DUMP_CONTEXT * 4 &&
		    addr <= sys_addr + MACH64_DUMP_CONTEXT * 4) ||
		    addr >= GETBUFADDR( buf ) + buf->used - MACH64_DUMP_CONTEXT * 4) {
			DRM_INFO("%08x:  0x%08x\n", addr, reg);
		}
		addr += 4;
		used--;
		
		count = (reg >> 16) + 1;
		reg = reg & 0xffff;
		reg = MMSELECT( reg );
		while ( count && used ) {
			if( addr <= GETBUFADDR( buf ) + MACH64_DUMP_CONTEXT * 4 ||
			    (addr >= sys_addr - MACH64_DUMP_CONTEXT * 4 &&
			    addr <= sys_addr + MACH64_DUMP_CONTEXT * 4) ||
			    addr >= GETBUFADDR( buf ) + buf->used - MACH64_DUMP_CONTEXT * 4) {
				DRM_INFO("%08x:    0x%04x = 0x%08x\n", addr, reg, le32_to_cpu(*p));
				skipped = 0;
			} else {
				if( !skipped ) {
					DRM_INFO( "  ...\n" );
					skipped = 1;
				}
			}
			p++;
			addr += 4;
			used--;
			
			reg += 4;
			count--;
		}
	}
	
	DRM_INFO( "\n" );
}

void mach64_dump_ring_info( drm_mach64_private_t *dev_priv )
{
	drm_mach64_descriptor_ring_t *ring = &dev_priv->ring;
	int i, skipped;
	
	DRM_INFO( "\n" );
	
	DRM_INFO("ring contents:\n");
	DRM_INFO("  head_addr: 0x%08x head: %u tail: %u\n\n", 
		 ring->head_addr, ring->head, ring->tail );
	
	skipped = 0;
	for ( i = 0; i < ring->size / sizeof(u32); i += 4) {
		if( i <= MACH64_DUMP_CONTEXT * 4 || 
		    i >= ring->size / sizeof(u32) - MACH64_DUMP_CONTEXT * 4 ||
		    (i >= ring->tail - MACH64_DUMP_CONTEXT * 4 &&
		    i <= ring->tail + MACH64_DUMP_CONTEXT * 4) ||
		    (i >= ring->head - MACH64_DUMP_CONTEXT * 4 &&
		    i <= ring->head + MACH64_DUMP_CONTEXT * 4)) {
			DRM_INFO( "  0x%08x:  0x%08x 0x%08x 0x%08x 0x%08x%s%s\n",
				ring->start_addr + i * sizeof(u32),
				le32_to_cpu(((u32*) ring->start)[i + 0]),
				le32_to_cpu(((u32*) ring->start)[i + 1]),
				le32_to_cpu(((u32*) ring->start)[i + 2]), 
				le32_to_cpu(((u32*) ring->start)[i + 3]),
				i == ring->head ? " (head)" : "",
				i == ring->tail ? " (tail)" : ""
			);
			skipped = 0;
		} else {
			if( !skipped ) {
				DRM_INFO( "  ...\n" );
				skipped = 1;
			}
		}
	}

	DRM_INFO( "\n" );
	
	if( ring->head >= 0 && ring->head < ring->size / sizeof(u32) ) {
		struct list_head *ptr;
		u32 addr = le32_to_cpu(((u32 *)ring->start)[ring->head + 1]);

		list_for_each(ptr, &dev_priv->pending) {
			drm_mach64_freelist_t *entry = 
				list_entry(ptr, drm_mach64_freelist_t, list);
			drm_buf_t *buf = entry->buf;

			u32 buf_addr = GETBUFADDR( buf );
			
			if ( buf_addr <= addr && addr < buf_addr + buf->used ) {
				mach64_dump_buf_info ( dev_priv, buf );
			}
		}
	}

	DRM_INFO( "\n" );
	DRM_INFO( "       BM_GUI_TABLE = 0x%08x\n", MACH64_READ( MACH64_BM_GUI_TABLE ) );
	DRM_INFO( "\n" );
	DRM_INFO( "BM_FRAME_BUF_OFFSET = 0x%08x\n", MACH64_READ( MACH64_BM_FRAME_BUF_OFFSET ) );
	DRM_INFO( " BM_SYSTEM_MEM_ADDR = 0x%08x\n", MACH64_READ( MACH64_BM_SYSTEM_MEM_ADDR ) );
	DRM_INFO( "         BM_COMMAND = 0x%08x\n", MACH64_READ( MACH64_BM_COMMAND ) );
	DRM_INFO( "\n" );
	DRM_INFO( "          BM_STATUS = 0x%08x\n", MACH64_READ( MACH64_BM_STATUS ) );
	DRM_INFO( "           BUS_CNTL = 0x%08x\n", MACH64_READ( MACH64_BUS_CNTL ) );
	DRM_INFO( "          FIFO_STAT = 0x%08x\n", MACH64_READ( MACH64_FIFO_STAT ) );
	DRM_INFO( "           GUI_STAT = 0x%08x\n", MACH64_READ( MACH64_GUI_STAT ) );
	DRM_INFO( "           SRC_CNTL = 0x%08x\n", MACH64_READ( MACH64_SRC_CNTL ) );
}


/* ================================================================
 * DMA test and initialization
 */

static int mach64_bm_dma_test( drm_device_t *dev )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	dma_addr_t data_handle;
	void *cpu_addr_data;
	u32 data_addr;
	u32 *table, *data;
	u32 expected[2];
	u32 src_cntl, pat_reg0, pat_reg1;
	int i, count, failed;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	table = (u32 *) dev_priv->ring.start;

	/* FIXME: get a dma buffer from the freelist here */
	DRM_DEBUG( "Allocating data memory ...\n" );
	cpu_addr_data = DRM(pci_alloc)( dev, 0x1000, 0x1000, 0xfffffffful, &data_handle );
	if (!cpu_addr_data || !data_handle) {
		DRM_INFO( "data-memory allocation failed!\n" );
		return DRM_ERR(ENOMEM);
	} else {
		data = (u32 *) cpu_addr_data;
		data_addr = (u32) data_handle;
	}

	/* Save the X server's value for SRC_CNTL and restore it
	 * in case our test fails.  This prevents the X server
	 * from disabling it's cache for this register
	 */
	src_cntl = MACH64_READ( MACH64_SRC_CNTL );
	pat_reg0 = MACH64_READ( MACH64_PAT_REG0 );
	pat_reg1 = MACH64_READ( MACH64_PAT_REG1 );

	mach64_do_wait_for_fifo( dev_priv, 3 );

	MACH64_WRITE( MACH64_SRC_CNTL, 0 );
	MACH64_WRITE( MACH64_PAT_REG0, 0x11111111 );
	MACH64_WRITE( MACH64_PAT_REG1, 0x11111111 );

	mach64_do_wait_for_idle( dev_priv );

	for (i=0; i < 2; i++) {
		u32 reg;
		reg = MACH64_READ( (MACH64_PAT_REG0 + i*4) );
		DRM_DEBUG( "(Before DMA Transfer) reg %d = 0x%08x\n", i, reg );
		if ( reg != 0x11111111 ) {
			DRM_INFO( "Error initializing test registers\n" );
			DRM_INFO( "resetting engine ...\n");
			mach64_do_engine_reset( dev_priv );
			DRM_INFO( "freeing data buffer memory.\n" );
			DRM(pci_free)( dev, 0x1000, cpu_addr_data, data_handle );
			return DRM_ERR(EIO);
		}
	}

	/* fill up a buffer with sets of 2 consecutive writes starting with PAT_REG0 */
	count = 0;

	data[count++] = cpu_to_le32(DMAREG(MACH64_PAT_REG0) | (1 << 16));
	data[count++] = expected[0] = 0x22222222;
	data[count++] = expected[1] = 0xaaaaaaaa;

	while (count < 1020) {
		data[count++] = cpu_to_le32(DMAREG(MACH64_PAT_REG0) | (1 << 16));
		data[count++] = 0x22222222;
		data[count++] = 0xaaaaaaaa;
	}
	data[count++] = cpu_to_le32(DMAREG(MACH64_SRC_CNTL) | (0 << 16));
	data[count++] = 0;

	DRM_DEBUG( "Preparing table ...\n" );
	table[MACH64_DMA_FRAME_BUF_OFFSET] = cpu_to_le32(MACH64_BM_ADDR + 
							 MACH64_APERTURE_OFFSET);
	table[MACH64_DMA_SYS_MEM_ADDR]     = cpu_to_le32(data_addr);
	table[MACH64_DMA_COMMAND]          = cpu_to_le32(count * sizeof( u32 ) 
							 | MACH64_DMA_HOLD_OFFSET 
							 | MACH64_DMA_EOL);
	table[MACH64_DMA_RESERVED]         = 0;

	DRM_DEBUG( "table[0] = 0x%08x\n", table[0] );
	DRM_DEBUG( "table[1] = 0x%08x\n", table[1] );
	DRM_DEBUG( "table[2] = 0x%08x\n", table[2] );
	DRM_DEBUG( "table[3] = 0x%08x\n", table[3] );

	for ( i = 0 ; i < 6 ; i++ ) {
		DRM_DEBUG( " data[%d] = 0x%08x\n", i, data[i] );
	}
	DRM_DEBUG( " ...\n" );
	for ( i = count-5 ; i < count ; i++ ) {
		DRM_DEBUG( " data[%d] = 0x%08x\n", i, data[i] );
	}

	DRM_MEMORYBARRIER();

	DRM_DEBUG( "waiting for idle...\n" );
	if ( ( i = mach64_do_wait_for_idle( dev_priv ) ) ) {
		DRM_INFO( "mach64_do_wait_for_idle failed (result=%d)\n", i);
		DRM_INFO( "resetting engine ...\n");
		mach64_do_engine_reset( dev_priv );
		mach64_do_wait_for_fifo( dev_priv, 3 );
		MACH64_WRITE( MACH64_SRC_CNTL, src_cntl );
		MACH64_WRITE( MACH64_PAT_REG0, pat_reg0 );
		MACH64_WRITE( MACH64_PAT_REG1, pat_reg1 );
		DRM_INFO( "freeing data buffer memory.\n" );
		DRM(pci_free)( dev, 0x1000, cpu_addr_data, data_handle );
		return i;
	}
	DRM_DEBUG( "waiting for idle...done\n" );

	DRM_DEBUG( "BUS_CNTL = 0x%08x\n", MACH64_READ( MACH64_BUS_CNTL ) );
	DRM_DEBUG( "SRC_CNTL = 0x%08x\n", MACH64_READ( MACH64_SRC_CNTL ) );
	DRM_DEBUG( "\n" );
	DRM_DEBUG( "data bus addr = 0x%08x\n", data_addr );
	DRM_DEBUG( "table bus addr = 0x%08x\n", dev_priv->ring.start_addr );

	DRM_DEBUG( "starting DMA transfer...\n" );
	MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD,
			  dev_priv->ring.start_addr |
			  MACH64_CIRCULAR_BUF_SIZE_16KB );

	MACH64_WRITE( MACH64_SRC_CNTL, 
		      MACH64_SRC_BM_ENABLE | MACH64_SRC_BM_SYNC |
		      MACH64_SRC_BM_OP_SYSTEM_TO_REG );

	/* Kick off the transfer */
	DRM_DEBUG( "starting DMA transfer... done.\n" );
	MACH64_WRITE( MACH64_DST_HEIGHT_WIDTH, 0 );

	DRM_DEBUG( "waiting for idle...\n" );

	if ( ( i = mach64_do_wait_for_idle( dev_priv ) ) ) {
		/* engine locked up, dump register state and reset */
		DRM_INFO( "mach64_do_wait_for_idle failed (result=%d)\n", i);
		mach64_dump_engine_info( dev_priv );
		DRM_INFO( "resetting engine ...\n");
		mach64_do_engine_reset( dev_priv );
		mach64_do_wait_for_fifo( dev_priv, 3 );
		MACH64_WRITE( MACH64_SRC_CNTL, src_cntl );
		MACH64_WRITE( MACH64_PAT_REG0, pat_reg0 );
		MACH64_WRITE( MACH64_PAT_REG1, pat_reg1 );
		DRM_INFO( "freeing data buffer memory.\n" );
		DRM(pci_free)( dev, 0x1000, cpu_addr_data, data_handle );
		return i;
	}

	DRM_DEBUG( "waiting for idle...done\n" );

	/* restore SRC_CNTL */
	mach64_do_wait_for_fifo( dev_priv, 1 );
	MACH64_WRITE( MACH64_SRC_CNTL, src_cntl );

	failed = 0;

	/* Check register values to see if the GUI master operation succeeded */
	for ( i = 0; i < 2; i++ ) {
		u32 reg;
		reg = MACH64_READ( (MACH64_PAT_REG0 + i*4) );
		DRM_DEBUG( "(After DMA Transfer) reg %d = 0x%08x\n", i, reg );
		if (reg != expected[i]) {
			failed = -1;
		}
	}

	/* restore pattern registers */
	mach64_do_wait_for_fifo( dev_priv, 2 );
	MACH64_WRITE( MACH64_PAT_REG0, pat_reg0 );
	MACH64_WRITE( MACH64_PAT_REG1, pat_reg1 );

	DRM_DEBUG( "freeing data buffer memory.\n" );
	DRM(pci_free)( dev, 0x1000, cpu_addr_data, data_handle );
	DRM_DEBUG( "returning ...\n" );

	return failed;
}


static int mach64_do_dma_init( drm_device_t *dev, drm_mach64_init_t *init )
{
	drm_mach64_private_t *dev_priv;
	u32 tmp;
	int i, ret;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	dev_priv = DRM(alloc)( sizeof(drm_mach64_private_t), DRM_MEM_DRIVER );
	if ( dev_priv == NULL )
		return DRM_ERR(ENOMEM);
	
	memset( dev_priv, 0, sizeof(drm_mach64_private_t) );

	dev_priv->is_pci	= init->is_pci;

	dev_priv->fb_bpp	= init->fb_bpp;
	dev_priv->front_offset  = init->front_offset;
	dev_priv->front_pitch   = init->front_pitch;
	dev_priv->back_offset   = init->back_offset;
	dev_priv->back_pitch    = init->back_pitch;

	dev_priv->depth_bpp     = init->depth_bpp;
	dev_priv->depth_offset  = init->depth_offset;
	dev_priv->depth_pitch   = init->depth_pitch;

	dev_priv->front_offset_pitch	= (((dev_priv->front_pitch/8) << 22) |
					   (dev_priv->front_offset >> 3));
	dev_priv->back_offset_pitch	= (((dev_priv->back_pitch/8) << 22) |
					   (dev_priv->back_offset >> 3));
	dev_priv->depth_offset_pitch	= (((dev_priv->depth_pitch/8) << 22) |
					   (dev_priv->depth_offset >> 3));

	dev_priv->usec_timeout		= 1000000;

	/* Set up the freelist, placeholder list and pending list */
	INIT_LIST_HEAD(&dev_priv->free_list);
	INIT_LIST_HEAD(&dev_priv->placeholders);
	INIT_LIST_HEAD(&dev_priv->pending);

	DRM_GETSAREA();

	if (!dev_priv->sarea) {
		DRM_ERROR("can not find sarea!\n");
		dev->dev_private = (void *)dev_priv;
	   	mach64_do_cleanup_dma(dev);
	   	return DRM_ERR(EINVAL);
	}
	dev_priv->fb = drm_core_findmap(dev, init->fb_offset);
	if (!dev_priv->fb) {
		DRM_ERROR("can not find frame buffer map!\n");
		dev->dev_private = (void *)dev_priv;
	   	mach64_do_cleanup_dma(dev);
	   	return DRM_ERR(EINVAL);
	}
	dev_priv->mmio = drm_core_findmap(dev, init->mmio_offset);
	if (!dev_priv->mmio) {
		DRM_ERROR("can not find mmio map!\n");
		dev->dev_private = (void *)dev_priv;
	   	mach64_do_cleanup_dma(dev);
	   	return DRM_ERR(EINVAL);
	}

	dev_priv->sarea_priv = (drm_mach64_sarea_t *)
		((u8 *)dev_priv->sarea->handle +
		 init->sarea_priv_offset);

	if( !dev_priv->is_pci ) {
		dev_priv->ring_map = drm_core_findmap(dev, init->ring_offset);
		if ( !dev_priv->ring_map ) {
			DRM_ERROR( "can not find ring map!\n" );
			dev->dev_private = (void *)dev_priv;
			mach64_do_cleanup_dma(dev);
			return DRM_ERR(EINVAL);
		}
		drm_core_ioremap( dev_priv->ring_map, dev );
		if ( !dev_priv->ring_map->handle ) {
		        DRM_ERROR( "can not ioremap virtual address for"
                                   " descriptor ring\n" );
			dev->dev_private = (void *) dev_priv;
			mach64_do_cleanup_dma( dev );
			return DRM_ERR(ENOMEM);
                }
		dev->agp_buffer_map = drm_core_findmap(dev, init->buffers_offset);
		if ( !dev->agp_buffer_map ) {
			DRM_ERROR( "can not find dma buffer map!\n" );
			dev->dev_private = (void *)dev_priv;
			mach64_do_cleanup_dma( dev );
			return DRM_ERR(EINVAL);
		}
		/* there might be a nicer way to do this - 
		   dev isn't passed all the way though the mach64 - DA */
		dev_priv->dev_buffers = dev->agp_buffer_map;

		drm_core_ioremap( dev->agp_buffer_map, dev );
		if ( !dev->agp_buffer_map->handle ) {
			DRM_ERROR( "can not ioremap virtual address for"
				   " dma buffer\n" );
			dev->dev_private = (void *) dev_priv;
			mach64_do_cleanup_dma( dev );
			return DRM_ERR(ENOMEM);
		}
		dev_priv->agp_textures = drm_core_findmap(dev, init->agp_textures_offset);
		if (!dev_priv->agp_textures) {
			DRM_ERROR( "can not find agp texture region!\n" );
			dev->dev_private = (void *)dev_priv;
			mach64_do_cleanup_dma( dev );
			return DRM_ERR(EINVAL);
		}
	}

	dev->dev_private = (void *) dev_priv;

	dev_priv->driver_mode = init->dma_mode;

	/* changing the FIFO size from the default causes problems with DMA */
	tmp = MACH64_READ( MACH64_GUI_CNTL );
	if ( (tmp & MACH64_CMDFIFO_SIZE_MASK) != MACH64_CMDFIFO_SIZE_128 ) {
		DRM_INFO( "Setting FIFO size to 128 entries\n");
		/* FIFO must be empty to change the FIFO depth */
		if ((ret=mach64_do_wait_for_idle( dev_priv ))) {
			DRM_ERROR("wait for idle failed before changing FIFO depth!\n");
			mach64_do_cleanup_dma( dev );
			return ret;
		}
		MACH64_WRITE( MACH64_GUI_CNTL, ( ( tmp & ~MACH64_CMDFIFO_SIZE_MASK ) \
						 | MACH64_CMDFIFO_SIZE_128 ) );
		/* need to read GUI_STAT for proper sync according to docs */
		if ((ret=mach64_do_wait_for_idle( dev_priv ))) {
			DRM_ERROR("wait for idle failed when changing FIFO depth!\n");
			mach64_do_cleanup_dma( dev );
			return ret;
		}
	}

	/* allocate descriptor memory from pci pool */
	DRM_DEBUG( "Allocating dma descriptor ring\n" );
	dev_priv->ring.size = 0x4000; /* 16KB */

	if ( dev_priv->is_pci ) {
		dev_priv->ring.start = DRM(pci_alloc)( dev, dev_priv->ring.size, 
						       dev_priv->ring.size, 0xfffffffful,
						       &dev_priv->ring.handle );

		if (!dev_priv->ring.start || !dev_priv->ring.handle) {
			DRM_ERROR( "Allocating dma descriptor ring failed\n");
			return DRM_ERR(ENOMEM);
		} else {
			dev_priv->ring.start_addr = (u32) dev_priv->ring.handle;
		}
        } else {
		dev_priv->ring.start = dev_priv->ring_map->handle;
		dev_priv->ring.start_addr = (u32) dev_priv->ring_map->offset;
	}

	memset( dev_priv->ring.start, 0, dev_priv->ring.size );
	DRM_INFO( "descriptor ring: cpu addr 0x%08x, bus addr: 0x%08x\n", 
		  (u32) dev_priv->ring.start, dev_priv->ring.start_addr );

	ret = 0;
	if ( dev_priv->driver_mode != MACH64_MODE_MMIO ) {

		/* enable block 1 registers and bus mastering */
		MACH64_WRITE( MACH64_BUS_CNTL, 
			      ( ( MACH64_READ(MACH64_BUS_CNTL) 
				  | MACH64_BUS_EXT_REG_EN ) 
				& ~MACH64_BUS_MASTER_DIS ) );

		/* try a DMA GUI-mastering pass and fall back to MMIO if it fails */
		DRM_DEBUG( "Starting DMA test...\n");
		if ( (ret=mach64_bm_dma_test( dev )) ) {
			dev_priv->driver_mode = MACH64_MODE_MMIO;
		}
	}

	switch (dev_priv->driver_mode) {
	case MACH64_MODE_MMIO:
		MACH64_WRITE( MACH64_BUS_CNTL, ( MACH64_READ(MACH64_BUS_CNTL) 
						 | MACH64_BUS_EXT_REG_EN
						 | MACH64_BUS_MASTER_DIS ) );
		if ( init->dma_mode == MACH64_MODE_MMIO )
			DRM_INFO( "Forcing pseudo-DMA mode\n" );
		else
			DRM_INFO( "DMA test failed (ret=%d), using pseudo-DMA mode\n", ret );
		break;
	case MACH64_MODE_DMA_SYNC:
		DRM_INFO( "DMA test succeeded, using synchronous DMA mode\n");
		break;
	case MACH64_MODE_DMA_ASYNC:
	default:
		DRM_INFO( "DMA test succeeded, using asynchronous DMA mode\n");
	}

	dev_priv->ring_running = 0;

	/* setup offsets for physical address of table start and end */
	dev_priv->ring.head_addr = dev_priv->ring.start_addr;
	dev_priv->ring.head = dev_priv->ring.tail = 0;
	dev_priv->ring.tail_mask = (dev_priv->ring.size / sizeof(u32)) - 1;
	dev_priv->ring.space = dev_priv->ring.size;

	/* setup physical address and size of descriptor table */
	mach64_do_wait_for_fifo( dev_priv, 1 );
	MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD, 
		      ( dev_priv->ring.head_addr | MACH64_CIRCULAR_BUF_SIZE_16KB ) );

	/* init frame counter */
	dev_priv->sarea_priv->frames_queued = 0;
	for (i = 0; i < MACH64_MAX_QUEUED_FRAMES; i++) {
		dev_priv->frame_ofs[i] = ~0; /* All ones indicates placeholder */
	}

	/* Allocate the DMA buffer freelist */
	if ( (ret=mach64_init_freelist( dev )) ) {
		DRM_ERROR("Freelist allocation failed\n");
		mach64_do_cleanup_dma( dev );
		return ret;
	}

	return 0;
}

/* ===================================================================
 * MMIO Pseudo-DMA (intended primarily for debugging, not performance)
 */

int mach64_do_dispatch_pseudo_dma( drm_mach64_private_t *dev_priv )
{
	drm_mach64_descriptor_ring_t *ring = &dev_priv->ring;
	volatile u32 *ring_read;
	struct list_head *ptr;
	drm_mach64_freelist_t *entry;
	drm_buf_t *buf = NULL;
	u32 *buf_ptr;
	u32 used, reg, target;
	int fifo, count, found, ret, no_idle_wait;

	fifo = count = reg = no_idle_wait = 0;
	target = MACH64_BM_ADDR;

	if ( (ret=mach64_do_wait_for_idle( dev_priv )) < 0) {
		DRM_INFO( "%s: idle failed before pseudo-dma dispatch, resetting engine\n", 
			  __FUNCTION__);
		mach64_dump_engine_info( dev_priv );
		mach64_do_engine_reset( dev_priv );
		return ret;
	}

	ring_read = (u32 *) ring->start;

	while ( ring->tail != ring->head ) {
		u32 buf_addr, new_target, offset; 
		u32 bytes, remaining, head, eol;

		head = ring->head;

		new_target = le32_to_cpu( ring_read[head++] ) - MACH64_APERTURE_OFFSET;
		buf_addr   = le32_to_cpu( ring_read[head++] );
		eol        = le32_to_cpu( ring_read[head]   ) & MACH64_DMA_EOL;
		bytes      = le32_to_cpu( ring_read[head++] )
			& ~(MACH64_DMA_HOLD_OFFSET | MACH64_DMA_EOL);
		head++;
		head &= ring->tail_mask;

		/* can't wait for idle between a blit setup descriptor 
		 * and a HOSTDATA descriptor or the engine will lock
		 */
		if (new_target == MACH64_BM_HOSTDATA && target == MACH64_BM_ADDR)
			no_idle_wait = 1;

		target = new_target;

		found = 0;
		offset = 0;
		list_for_each(ptr, &dev_priv->pending)
		{
			entry = list_entry(ptr, drm_mach64_freelist_t, list);
			buf = entry->buf;
			offset = buf_addr - GETBUFADDR( buf );
			if (offset >= 0 && offset < MACH64_BUFFER_SIZE) {
				found = 1;
				break;
			}
		}

		if (!found || buf == NULL) {
			DRM_ERROR("Couldn't find pending buffer: head: %u tail: %u buf_addr: 0x%08x %s\n",
				  head, ring->tail, buf_addr, (eol ? "eol" : ""));
			mach64_dump_ring_info( dev_priv );
			mach64_do_engine_reset( dev_priv );
			return DRM_ERR(EINVAL);
		}

		/* Hand feed the buffer to the card via MMIO, waiting for the fifo 
		 * every 16 writes 
		 */
		DRM_DEBUG("target: (0x%08x) %s\n", target, 
				 (target == MACH64_BM_HOSTDATA ? "BM_HOSTDATA" : "BM_ADDR"));
		DRM_DEBUG("offset: %u bytes: %u used: %u\n", offset, bytes, buf->used);

		remaining = (buf->used - offset) >> 2; /* dwords remaining in buffer */
		used = bytes >> 2; /* dwords in buffer for this descriptor */
		buf_ptr = (u32 *)((char *)GETBUFPTR( buf ) + offset);

		while ( used ) {
			
			if (count == 0) {
				if (target == MACH64_BM_HOSTDATA) {
					reg = DMAREG(MACH64_HOST_DATA0);
					count = (remaining > 16) ? 16 : remaining;
					fifo = 0;
				} else {
					reg = le32_to_cpu(*buf_ptr++);
					used--;
					count = (reg >> 16) + 1;
				}

				reg = reg & 0xffff;
				reg = MMSELECT( reg );
			}
			while ( count && used ) {
				if ( !fifo ) {
					if (no_idle_wait) {
						if ( (ret=mach64_do_wait_for_fifo( dev_priv, 16 )) < 0 ) {
							no_idle_wait = 0;
							return ret;
						}
					} else {
						if ( (ret=mach64_do_wait_for_idle( dev_priv )) < 0 ) {
							return ret;
						}
					}
					fifo = 16;
				}
				--fifo;
				MACH64_WRITE(reg, le32_to_cpu(*buf_ptr++));
				used--; remaining--;
					
				reg += 4;
				count--;
			}
		}
		ring->head = head;
		ring->head_addr = ring->start_addr + (ring->head * sizeof(u32));
		ring->space += (4 * sizeof(u32));
	}

	if ( (ret=mach64_do_wait_for_idle( dev_priv )) < 0 ) {
		return ret;
	}
	MACH64_WRITE( MACH64_BM_GUI_TABLE_CMD, 
		      ring->head_addr | MACH64_CIRCULAR_BUF_SIZE_16KB );

	DRM_DEBUG( "%s completed\n", __FUNCTION__ );
	return 0;
}

/* ================================================================
 * DMA cleanup
 */

int mach64_do_cleanup_dma( drm_device_t *dev )
{
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	/* Make sure interrupts are disabled here because the uninstall ioctl
	 * may not have been called from userspace and after dev_private
	 * is freed, it's too late.
	 */
	if ( dev->irq ) DRM(irq_uninstall)(dev);

	if ( dev->dev_private ) {
		drm_mach64_private_t *dev_priv = dev->dev_private;

		if ( dev_priv->is_pci ) {
			if ( (dev_priv->ring.start != NULL) && dev_priv->ring.handle ) {
				DRM(pci_free)( dev, dev_priv->ring.size, 
					       dev_priv->ring.start, dev_priv->ring.handle );
			}
		} else {
			if ( dev_priv->ring_map )
				drm_core_ioremapfree( dev_priv->ring_map, dev );
		}

		if ( dev->agp_buffer_map ) {
			drm_core_ioremapfree( dev->agp_buffer_map, dev );
			dev->agp_buffer_map = NULL;
		}

		mach64_destroy_freelist( dev );

		DRM(free)( dev_priv, sizeof(drm_mach64_private_t),
			   DRM_MEM_DRIVER );
		dev->dev_private = NULL;
	}

	return 0;
}

/* ================================================================
 * IOCTL handlers
 */

int mach64_dma_init( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_mach64_init_t init;
		
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev, filp );

	DRM_COPY_FROM_USER_IOCTL( init, (drm_mach64_init_t *)data, 
	    sizeof(init) );

	switch ( init.func ) {
	case DRM_MACH64_INIT_DMA:
		return mach64_do_dma_init( dev, &init );
	case DRM_MACH64_CLEANUP_DMA:
		return mach64_do_cleanup_dma( dev );
	}
		
	return DRM_ERR(EINVAL);
}

int mach64_dma_idle( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_mach64_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev, filp );

	return mach64_do_dma_idle( dev_priv );
}

int mach64_dma_flush( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_mach64_private_t *dev_priv = dev->dev_private;

	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev, filp );

	return mach64_do_dma_flush( dev_priv );
}

int mach64_engine_reset( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	
	DRM_DEBUG( "%s\n", __FUNCTION__ );

	LOCK_TEST_WITH_RETURN( dev, filp );

	return mach64_do_engine_reset( dev_priv );
}


/* ================================================================
 * Freelist management
 */

int mach64_init_freelist( drm_device_t *dev )
{
	drm_device_dma_t *dma = dev->dma;
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_freelist_t *entry;
	struct list_head *ptr;
	int i;

	DRM_DEBUG("%s: adding %d buffers to freelist\n", __FUNCTION__, dma->buf_count);

	for ( i = 0 ; i < dma->buf_count ; i++ ) {
		if ((entry = 
		     (drm_mach64_freelist_t *) DRM(alloc)(sizeof(drm_mach64_freelist_t), 
							  DRM_MEM_BUFLISTS)) == NULL)
			return DRM_ERR(ENOMEM);
		memset( entry, 0, sizeof(drm_mach64_freelist_t) );
		entry->buf = dma->buflist[i];
		ptr = &entry->list;
		list_add_tail(ptr, &dev_priv->free_list);
	}

	return 0;
}

void mach64_destroy_freelist( drm_device_t *dev )
{
	drm_mach64_private_t *dev_priv = dev->dev_private;
	drm_mach64_freelist_t *entry;
	struct list_head *ptr;
	struct list_head *tmp;

	DRM_DEBUG("%s\n", __FUNCTION__);

	list_for_each_safe(ptr, tmp, &dev_priv->pending)
	{
		list_del(ptr);
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		DRM(free)(entry, sizeof(*entry), DRM_MEM_BUFLISTS);
	}
	list_for_each_safe(ptr, tmp, &dev_priv->placeholders)
	{
		list_del(ptr);
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		DRM(free)(entry, sizeof(*entry), DRM_MEM_BUFLISTS);
	}

	list_for_each_safe(ptr, tmp, &dev_priv->free_list)
	{
		list_del(ptr);
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		DRM(free)(entry, sizeof(*entry), DRM_MEM_BUFLISTS);
	}
}

/* IMPORTANT: This function should only be called when the engine is idle or locked up, 
 * as it assumes all buffers in the pending list have been completed by the hardware.
 */
int mach64_do_release_used_buffers( drm_mach64_private_t *dev_priv )
{
	struct list_head *ptr;
	struct list_head *tmp;
	drm_mach64_freelist_t *entry;
	int i;

	if ( list_empty(&dev_priv->pending) )
		return 0;

	/* Iterate the pending list and move all buffers into the freelist... */
	i = 0;
	list_for_each_safe(ptr, tmp, &dev_priv->pending)
	{
		entry = list_entry(ptr, drm_mach64_freelist_t, list);
		if (entry->discard) {
			entry->buf->pending = 0;
			list_del(ptr);
			list_add_tail(ptr, &dev_priv->free_list);
			i++;
		}
	}

	DRM_DEBUG( "%s: released %d buffers from pending list\n", __FUNCTION__, i );

        return 0;
}

drm_buf_t *mach64_freelist_get( drm_mach64_private_t *dev_priv )
{
	drm_mach64_descriptor_ring_t *ring = &dev_priv->ring;
	drm_mach64_freelist_t *entry;
	struct list_head *ptr;
	struct list_head *tmp;
	int t;

	if ( list_empty(&dev_priv->free_list) ) {
		u32 head, tail, ofs;

		if ( list_empty( &dev_priv->pending ) ) {
			DRM_ERROR( "Couldn't get buffer - pending and free lists empty\n" );
			t = 0;
			list_for_each( ptr, &dev_priv->placeholders ) {
				t++;
			}
			DRM_INFO( "Placeholders: %d\n", t );
			return NULL;
		}

		tail = ring->tail;
		for ( t = 0 ; t < dev_priv->usec_timeout ; t++ ) {
			mach64_ring_tick( dev_priv, ring );
			head = ring->head;

			if ( head == tail ) {
#if MACH64_EXTRA_CHECKING
				if ( MACH64_READ(MACH64_GUI_STAT) & MACH64_GUI_ACTIVE ) {
					DRM_ERROR( "Empty ring with non-idle engine!\n" );
					mach64_dump_ring_info( dev_priv );
					return NULL;
				}
#endif
				/* last pass is complete, so release everything */
				mach64_do_release_used_buffers( dev_priv );
				DRM_DEBUG( "%s: idle engine, freed all buffers.\n", __FUNCTION__ );
				if ( list_empty(&dev_priv->free_list) ) {
					DRM_ERROR( "Freelist empty with idle engine\n" );
					return NULL;
				}
				goto _freelist_entry_found;
			}
			/* Look for a completed buffer and bail out of the loop 
			 * as soon as we find one -- don't waste time trying
			 * to free extra bufs here, leave that to do_release_used_buffers
			 */
			list_for_each_safe(ptr, tmp, &dev_priv->pending) {
				entry = list_entry(ptr, drm_mach64_freelist_t, list);
				ofs = entry->ring_ofs;
				if ( entry->discard &&
				     ((head < tail && (ofs < head || ofs >= tail)) ||
				      (head > tail && (ofs < head && ofs >= tail))) ) {
#if MACH64_EXTRA_CHECKING
					int i;
					
					for ( i = head ; i != tail ; i = (i + 4) & ring->tail_mask ) {
						u32 o1 = le32_to_cpu(((u32 *)ring->start)[i + 1]);
						u32 o2 = GETBUFADDR( entry->buf );

						if ( o1 == o2 ) {
							DRM_ERROR ( "Attempting to free used buffer: "
								    "i=%d  buf=0x%08x\n", i, o1 );
							mach64_dump_ring_info( dev_priv );
							return NULL;
						}
					}
#endif
					/* found a processed buffer */
					entry->buf->pending = 0;
					list_del(ptr);
					entry->buf->used = 0;
					list_add_tail(ptr, &dev_priv->placeholders);
					DRM_DEBUG( "%s: freed processed buffer (head=%d tail=%d "
						   "buf ring ofs=%d).\n", __FUNCTION__, head, tail, ofs );
					return entry->buf;
				}
			}
			DRM_UDELAY( 1 );
		}
		mach64_dump_ring_info( dev_priv );
		DRM_ERROR( "timeout waiting for buffers: ring head_addr: 0x%08x head: %d tail: %d\n", 
			   ring->head_addr, ring->head, ring->tail );
		return NULL;
	}

_freelist_entry_found:
	ptr = dev_priv->free_list.next;
	list_del(ptr);
	entry = list_entry(ptr, drm_mach64_freelist_t, list);
	entry->buf->used = 0;
	list_add_tail(ptr, &dev_priv->placeholders);
	return entry->buf;
}

/* ================================================================
 * DMA buffer request and submission IOCTL handler
 */

static int mach64_dma_get_buffers( DRMFILE filp, drm_device_t *dev, drm_dma_t *d )
{
	int i;
	drm_buf_t *buf;
	drm_mach64_private_t *dev_priv = dev->dev_private;

	for ( i = d->granted_count ; i < d->request_count ; i++ ) {
		buf = mach64_freelist_get( dev_priv );
#if MACH64_EXTRA_CHECKING
		if ( !buf ) return DRM_ERR(EFAULT);
#else
		if ( !buf ) return DRM_ERR(EAGAIN);
#endif

		buf->filp = filp;

		if ( DRM_COPY_TO_USER( &d->request_indices[i], &buf->idx,
				   sizeof(buf->idx) ) )
			return DRM_ERR(EFAULT);
		if ( DRM_COPY_TO_USER( &d->request_sizes[i], &buf->total,
				   sizeof(buf->total) ) )
			return DRM_ERR(EFAULT);

		d->granted_count++;
	}
	return 0;
}

int mach64_dma_buffers( DRM_IOCTL_ARGS )
{
	DRM_DEVICE;
	drm_device_dma_t *dma = dev->dma;
	drm_dma_t d;
        int ret = 0;

        LOCK_TEST_WITH_RETURN( dev, filp );

	DRM_COPY_FROM_USER_IOCTL( d, (drm_dma_t *)data, sizeof(d) );

        /* Please don't send us buffers.
	 */
        if ( d.send_count != 0 ) 
        {
		DRM_ERROR( "Process %d trying to send %d buffers via drmDMA\n",
			   DRM_CURRENTPID, d.send_count );
		return DRM_ERR(EINVAL);
	}

	/* We'll send you buffers.
	 */
	if ( d.request_count < 0 || d.request_count > dma->buf_count ) 
	{
		DRM_ERROR( "Process %d trying to get %d buffers (of %d max)\n",
			   DRM_CURRENTPID, d.request_count, dma->buf_count );
		ret = DRM_ERR(EINVAL);
	}
        
	d.granted_count = 0;

	if ( d.request_count ) 
	{
		ret = mach64_dma_get_buffers( filp, dev, &d );
	}

	DRM_COPY_TO_USER_IOCTL( (drm_dma_t *)data, d, sizeof(d) );

        return ret;
}

static void mach64_driver_pretakedown(drm_device_t *dev)
{
	mach64_do_cleanup_dma( dev );					
}

void mach64_driver_register_fns(drm_device_t *dev)
{
	dev->driver_features = DRIVER_USE_AGP | DRIVER_USE_MTRR | DRIVER_PCI_DMA | DRIVER_HAVE_DMA | DRIVER_HAVE_IRQ | DRIVER_IRQ_SHARED | DRIVER_IRQ_VBL;
	dev->fn_tbl.pretakedown = mach64_driver_pretakedown;
	dev->fn_tbl.vblank_wait = mach64_driver_vblank_wait;
	dev->fn_tbl.irq_preinstall = mach64_driver_irq_preinstall;
	dev->fn_tbl.irq_postinstall = mach64_driver_irq_postinstall;
	dev->fn_tbl.irq_uninstall = mach64_driver_irq_uninstall;
	dev->fn_tbl.irq_handler = mach64_driver_irq_handler;
}
