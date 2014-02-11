/* ------------------------------------------------------------------------- */
/* acq400_sysfs.c D-TACQ ACQ400 series driver, sysfs (knobs)	             */
/* ------------------------------------------------------------------------- */
/*   Copyright (C) 2010 Peter Milne, D-TACQ Solutions Ltd
 *                      <Peter dot Milne at D hyphen TACQ dot com>

    This program is free software; you can redistribute it and/or modify
    it under the terms of Version 2 of the GNU General Public License
    as published by the Free Software Foundation;

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.                */
/* ------------------------------------------------------------------------- */


/** @file rtm-t-sysfs.c D-TACQ PCIe RTM_T driver, sysfs (knobs) */



#include <linux/device.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/list.h>
#include <linux/pci.h>
#include <linux/time.h>
#include <linux/init.h>
#include <linux/timex.h>
#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/moduleparam.h>
#include <linux/mutex.h>

#include <linux/proc_fs.h>
#include <linux/seq_file.h>

#include <asm/uaccess.h>  /* VERIFY_READ|WRITE */

#include "lk-shim.h"


#include <linux/device.h>
#include <linux/module.h>
#include <linux/user.h>

#include "acq400.h"

#define DEVICE_CREATE_FILE(dev, attr) 							\
	do {										\
		if (device_create_file(dev, attr)){ 					\
			dev_warn(dev, "%s:%d device_create_file", __FILE__, __LINE__); 	\
		} 									\
	} while (0)


static ssize_t show_clkdiv(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	u32 clkdiv = acq400rd32(acq400_devices[dev->id], ADC_CLKDIV);
	return sprintf(buf, "%u\n", clkdiv);
}

static ssize_t store_clkdiv(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	u32 clkdiv;
	if (sscanf(buf, "%u", &clkdiv) == 1 &&
	    clkdiv >= 1 &&
	    clkdiv <= ADC_CLK_DIV_MASK){
		acq400wr32(acq400_devices[dev->id], ADC_CLKDIV, clkdiv);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(clkdiv, S_IRUGO|S_IWUGO, show_clkdiv, store_clkdiv);

unsigned get_gain(u32 gains, int chan)
{
	return (gains >> chan*2) & 0x3;
}

unsigned set_gain(u32 gains, int chan, u32 value)
{
	u32 mask = 0x3 << chan*2;
	gains &= ~mask;
	gains |= (value&0x3) << chan*2;
	return gains;
}
static ssize_t show_gains(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	u32 gains = acq400rd32(acq400_devices[dev->id], ADC_GAIN);
	return sprintf(buf, "%u%u%u%u\n",
			get_gain(gains, 0), get_gain(gains, 1),
			get_gain(gains, 2), get_gain(gains, 3));
}

static ssize_t store_gains(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	char gx[4];

	if (sscanf(buf, "%c%c%c%c", gx+0, gx+1, gx+2, gx+3) == 4){
		u32 gains = acq400rd32(acq400_devices[dev->id], ADC_GAIN);
		int ic;
		for (ic = 0; ic < 4; ++ic){
			if (gx[ic] >= '0' && gx[ic] <= '3'){
				gains = set_gain(gains, ic, gx[ic]-'0');
			}else{
				switch(gx[ic]) {
				case '-':
				case 'x':
				case 'X':
					break;
				default:
					dev_warn(dev, "bad value at %c at %d", gx[ic], ic);
				}
			}
		}
		dev_dbg(dev, "set gains: %02x", gains);
		acq400wr32(acq400_devices[dev->id], ADC_GAIN, gains);
		return count;
	}else{
		dev_warn(dev, "rejecting input args != 4");
		return -1;
	}
}

static DEVICE_ATTR(gains, S_IRUGO|S_IWUGO, show_gains, store_gains);


static ssize_t show_gain(
	int chan,
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	u32 gains = acq400rd32(acq400_devices[dev->id], ADC_GAIN);

	return sprintf(buf, "%u\n", get_gain(gains, chan));
}

static ssize_t store_gain(
		int chan,
		struct device * dev,
		struct device_attribute *attr,
		const char * buf,
		size_t count)
{
	char gx;

	if (sscanf(buf, "%c", &gx) == 1){
		u32 gains = acq400rd32(acq400_devices[dev->id], ADC_GAIN);
		if (gx >= '0' && gx <= '3'){
			gains = set_gain(gains, chan, gx-'0');
		}else{
			switch(gx) {
			case '-':
			case 'x':
			case 'X':
				break;
			default:
				dev_warn(dev, "bad value at %c at %d", gx, chan);
			}
		}

		dev_dbg(dev, "set gain: %02x", gains);
		acq400wr32(acq400_devices[dev->id], ADC_GAIN, gains);
		return count;
	}else{
		dev_warn(dev, "rejecting input args != 4");
		return -1;
	}
}

#define MAKE_GAIN(CH)							\
static ssize_t show_gain##CH(						\
	struct device * dev,						\
	struct device_attribute *attr,					\
	char * buf)							\
{									\
	return show_gain(CH - 1, dev, attr, buf);			\
}									\
									\
static ssize_t store_gain##CH(						\
	struct device * dev,						\
	struct device_attribute *attr,					\
	const char * buf,						\
	size_t count)							\
{									\
	return store_gain(CH - 1, dev, attr, buf, count);		\
}									\
static DEVICE_ATTR(gain##CH, S_IRUGO|S_IWUGO, show_gain##CH, store_gain##CH)

MAKE_GAIN(1);
MAKE_GAIN(2);
MAKE_GAIN(3);
MAKE_GAIN(4);

static ssize_t show_signal(
	struct device * dev,
	struct device_attribute *attr,
	char * buf,
	unsigned REG,
	int shl, int mbit,
	const char*signame, const char* mbit_hi, const char* mbit_lo)
{
	u32 adc_ctrl = acq400rd32(acq400_devices[dev->id], REG);
	if (adc_ctrl&mbit){
		u32 sel = (adc_ctrl >> shl) & CTRL_SIG_MASK;
		unsigned dx = sel&CTRL_SIG_SEL;
		int rising = ((adc_ctrl >> shl) & CTRL_SIG_RISING) != 0;

		return sprintf(buf, "%s=%d,%d,%d %s d%u %s\n",
				signame, 1, dx, rising,
				mbit_hi, dx, rising? "RISING": "FALLING");
	}else{
		return sprintf(buf, "%s=0,0,0 %s\n", signame, mbit_lo);
	}
}

int store_signal3(struct device* dev,
		unsigned REG,
		int shl, int mbit,
		unsigned imode, unsigned dx, unsigned rising)
{
	struct acq400_dev* adev = acq400_devices[dev->id];

	if (adev->busy){
		return -EBUSY;
	}else{
		u32 adc_ctrl = acq400rd32(adev, REG);

		switch(imode){
		case 0:
			adc_ctrl &= ~mbit;
			break;
		case 1:
			if (dx > 7){
				dev_warn(dev, "rejecting \"%u\" dx > 7", dx);
				return -1;
			}
			adc_ctrl &= ~(CTRL_SIG_MASK << shl);
			adc_ctrl |=  (dx|(rising? CTRL_SIG_RISING:0))<<shl;
			adc_ctrl |= mbit;
			break;
		default:
			dev_warn(dev, "BAD mode:%u", imode);
			return -1;
		}
		acq400wr32(adev, REG, adc_ctrl);
		return 0;
	}
}
static ssize_t store_signal(
		struct device * dev,
		struct device_attribute *attr,
		const char * buf,
		size_t count,
		unsigned REG,
		int shl, int mbit, const char* mbit_hi, const char* mbit_lo)
{
	char sense;
	char mode[16];
	unsigned imode, dx, rising;

	/* first form: imode,dx,rising : easiest with auto eg StreamDevice */
	int nscan = sscanf(buf, "%u,%u,%u", &imode, &dx, &rising);
	if (nscan == 3){
		if (store_signal3(dev, REG, shl, mbit, imode, dx, rising)){
			return -1;
		}else{
			return count;
		}
	}

	/* second form: mode dDX sense : better for human scripting */
	nscan = sscanf(buf, "%10s d%u %c", mode, &dx, &sense);

	switch(nscan){
	case 1:
		if (strcmp(mode, mbit_lo) == 0){
			return store_signal3(dev, REG, shl, mbit, 0, 0, 0);
		}
		dev_warn(dev, "single arg must be:\"%s\"", mbit_lo);
		return -1;
	case 3:
		if (strcmp(mode, mbit_hi) == 0){
			unsigned falling = strchr("Ff-Nn", sense) != NULL;
			rising 	= strchr("Rr+Pp", sense) != NULL;

			if (!rising && !falling){
				dev_warn(dev,
				 "rejecting \"%s\" sense must be R or F", buf);
				return -1;
			}else if (store_signal3(dev, REG, shl, mbit, 1, dx, rising)){
				return -1;
			}else{
				return count;
			}
		}
		/* else fall thru */
	default:
		dev_warn(dev, "%s|%s dX R|F", mbit_lo, mbit_hi);
		return -1;
	}
}

#define MAKE_SIGNAL(SIGNAME, REG, shl, mbit, HI, LO)			\
static ssize_t show_##SIGNAME(						\
	struct device * dev,						\
	struct device_attribute *attr,					\
	char * buf)							\
{									\
	return show_signal(dev, attr, buf, REG, shl, mbit, #SIGNAME, HI, LO);\
}									\
									\
static ssize_t store_##SIGNAME(						\
	struct device * dev,						\
	struct device_attribute *attr,					\
	const char * buf,						\
	size_t count)							\
{									\
	return store_signal(dev, attr, buf, count, REG, shl, mbit, HI, LO);\
}									\
static DEVICE_ATTR(SIGNAME, S_IRUGO|S_IWUGO, 				\
		show_##SIGNAME, store_##SIGNAME)

#define ENA	"enable"
#define DIS	"disable"
#define EXT	"external"
#define SOFT	"soft"
#define INT	"internal"
MAKE_SIGNAL(event1, TIM_CTRL, TIM_CTRL_EVENT1_SHL, TIM_CTRL_MODE_EV1_EN, ENA, DIS);
MAKE_SIGNAL(event0, TIM_CTRL, TIM_CTRL_EVENT0_SHL, TIM_CTRL_MODE_EV0_EN, ENA, DIS);
MAKE_SIGNAL(trg,    TIM_CTRL, TIM_CTRL_TRIG_SHL,   TIM_CTRL_MODE_HW_TRG, EXT, SOFT);
MAKE_SIGNAL(clk,    TIM_CTRL, TIM_CTRL_CLK_SHL,	   TIM_CTRL_MODE_HW_CLK, EXT, INT);
MAKE_SIGNAL(sync,   TIM_CTRL, TIM_CTRL_SYNC_SHL,   TIM_CTRL_MODE_SYNC,   EXT, INT);


MAKE_SIGNAL(gpg_trg, GPG_CONTROL, GPG_CTRL_TRG_SHL, GPG_CTRL_EXT_TRG, EXT, SOFT);
MAKE_SIGNAL(gpg_clk, GPG_CONTROL, GPG_CTRL_CLK_SHL, GPG_CTRL_EXTCLK,  EXT, INT);
MAKE_SIGNAL(gpg_sync,GPG_CONTROL, GPG_CTRL_SYNC_SHL, GPG_CTRL_EXT_SYNC, EXT, SOFT);


static ssize_t show_gpg_top(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev* adev = acq400_devices[dev->id];
	u32 gpg_ctrl = acq400rd32(adev, GPG_CONTROL);
	u32 gpg_top = (gpg_ctrl&GPG_CTRL_TOPADDR) >> GPG_CTRL_TOPADDR_SHL;
	return sprintf(buf, "%u\n", gpg_top);
}

static ssize_t store_gpg_top(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev* adev = acq400_devices[dev->id];
	u32 gpg_top;

	if (sscanf(buf, "%u", &gpg_top) == 1){
		set_gpg_top(adev, gpg_top);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(gpg_top, S_IRUGO|S_IWUGO, show_gpg_top, store_gpg_top);


static ssize_t show_gpg_mode(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev* adev = acq400_devices[dev->id];
	u32 gpg_ctrl = acq400rd32(adev, GPG_CONTROL);
	u32 gpg_mode = (gpg_ctrl&GPG_CTRL_MODE) >> GPG_CTRL_MODE_SHL;
	return sprintf(buf, "%u\n", gpg_mode);
}

static ssize_t store_gpg_mode(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev* adev = acq400_devices[dev->id];
	u32 gpg_mode;

	if (sscanf(buf, "%u", &gpg_mode) == 1){
		u32 gpg_ctrl = acq400rd32(adev, GPG_CONTROL);
		gpg_mode <<= GPG_CTRL_MODE_SHL;
		gpg_mode &= GPG_CTRL_MODE;
		gpg_ctrl &= ~GPG_CTRL_MODE;
		gpg_ctrl |= gpg_mode;

		acq400wr32(adev, GPG_CONTROL, gpg_ctrl);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(gpg_mode, S_IRUGO|S_IWUGO, show_gpg_mode, store_gpg_mode);

static ssize_t show_gpg_enable(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev* adev = acq400_devices[dev->id];
	u32 gpg_ctrl = acq400rd32(adev, GPG_CONTROL);
	return sprintf(buf, "%u\n", (gpg_ctrl&GPG_CTRL_ENABLE) != 0);
}

static ssize_t store_gpg_enable(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev* adev = acq400_devices[dev->id];
	u32 gpg_enable;

	if (sscanf(buf, "%u", &gpg_enable) == 1){
		u32 gpg_ctrl = acq400rd32(adev, GPG_CONTROL);
		if (gpg_enable){
			gpg_ctrl |= GPG_CTRL_ENABLE;
		}else{
			gpg_ctrl &= ~GPG_CTRL_ENABLE;
		}
		acq400wr32(adev, GPG_CONTROL, gpg_ctrl);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(gpg_enable, S_IRUGO|S_IWUGO, show_gpg_enable, store_gpg_enable);

static ssize_t show_simulate(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	u32 simulate = acq400_devices[dev->id]->ramp_en != 0;
	return sprintf(buf, "%u\n", simulate);
}

static ssize_t store_simulate(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	u32 simulate;
	if (sscanf(buf, "%u", &simulate) == 1){
		acq400_devices[dev->id]->ramp_en = simulate != 0;
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(simulate, S_IRUGO|S_IWUGO, show_simulate, store_simulate);

static ssize_t show_spad(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	u32 spad = acq400_devices[dev->id]->spad_en != 0;
	return sprintf(buf, "%u\n", spad);
}

static ssize_t store_spad(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	u32 spad;
	if (sscanf(buf, "%u", &spad) == 1){
		acq400_devices[dev->id]->spad_en = spad != 0;
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(spad, S_IRUGO|S_IWUGO, show_spad, store_spad);

static ssize_t show_spadN(
	struct device * dev,
	struct device_attribute *attr,
	char * buf,
	const int ix)
{
	struct acq400_dev* adev = acq400_devices[dev->id];
	u32 spad = acq400rd32(adev, SPADN(ix));
	return sprintf(buf, "0x%08x\n", spad);
}

static ssize_t store_spadN(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count,
	const int ix)
{
	u32 spad;
	if (sscanf(buf, "%x", &spad) == 1){
		struct acq400_dev* adev = acq400_devices[dev->id];
		acq400wr32(adev, SPADN(ix), spad);
		return count;
	}else{
		return -1;
	}
}


#define MAKE_SPAD(IX)							\
static ssize_t show_spad##IX(						\
	struct device * dev,						\
	struct device_attribute *attr,					\
	char * buf)							\
{									\
	return show_spadN(dev, attr, buf, IX);				\
}									\
									\
static ssize_t store_spad##IX(						\
	struct device * dev,						\
	struct device_attribute *attr,					\
	const char * buf,						\
	size_t count)							\
{									\
	return store_spadN(dev, attr, buf, count, IX);			\
}									\
static DEVICE_ATTR(spad##IX, S_IRUGO|S_IWUGO, 				\
		show_spad##IX, store_spad##IX)

MAKE_SPAD(0);
MAKE_SPAD(1);
MAKE_SPAD(2);
MAKE_SPAD(3);
MAKE_SPAD(4);
MAKE_SPAD(5);
MAKE_SPAD(6);
MAKE_SPAD(7);


static ssize_t show_nbuffers(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->nbuffers);
}

static DEVICE_ATTR(nbuffers, S_IRUGO, show_nbuffers, 0);

static ssize_t show_bufferlen(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->bufferlen);
}

static DEVICE_ATTR(bufferlen, S_IRUGO, show_bufferlen, 0);

static ssize_t show_hitide(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->hitide);
}

static DEVICE_ATTR(hitide, S_IRUGO, show_hitide, 0);

static ssize_t show_lotide(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->lotide);
}

static DEVICE_ATTR(lotide, S_IRUGO, show_lotide, 0);


static ssize_t show_data32(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->data32);
}

static ssize_t store_data32(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	u32 data32;
	if (!IS_ACQ43X(adev) && sscanf(buf, "%u", &data32) == 1){
		adev->data32 = data32 != 0;
		adev->word_size = data32? 4: 2;
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(data32, S_IRUGO|S_IWUGO, show_data32, store_data32);


static ssize_t show_stats(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct STATS *stats = &acq400_devices[dev->id]->stats;
	return sprintf(buf, "fifo_ints=%u dma_transactions=%u\n",
			stats->fifo_interrupts, stats->dma_transactions);
}

static ssize_t store_stats(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct STATS *stats = &acq400_devices[dev->id]->stats;
	u32 clr;

	if (sscanf(buf, "%u", &clr) == 1){
		if (clr){
			memset(stats, 0, sizeof(struct STATS));
		}
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(stats, S_IRUGO|S_IWUGO, show_stats, store_stats);

static ssize_t show_shot(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%d\n", adev->stats.shot);
}

static ssize_t store_shot(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	u32 set;

	if (sscanf(buf, "%u", &set) == 1){
		adev->stats.shot = set;
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(shot, S_IRUGO|S_IWUGO, show_shot, store_shot);

static ssize_t show_run(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%d\n", adev->stats.run);
}


static DEVICE_ATTR(run, S_IRUGO, show_run, 0);


static ssize_t show_clk_count(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	u32 counter = acq400rd32_upcount(acq400_devices[dev->id], ADC_CLK_CTR);
	return sprintf(buf, "%u\n", counter&ADC_SAMPLE_CTR_MASK);
}

static DEVICE_ATTR(clk_count, S_IRUGO|S_IWUGO, show_clk_count, 0);

static ssize_t show_sample_count(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	u32 count = acq400rd32_upcount(acq400_devices[dev->id], ADC_SAMPLE_CTR);
	return sprintf(buf, "%u\n", count&ADC_SAMPLE_CTR_MASK);
}

static DEVICE_ATTR(sample_count, S_IRUGO|S_IWUGO, show_sample_count, 0);

static ssize_t show_clk_counter_src(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	u32 counter = acq400rd32(acq400_devices[dev->id], ADC_CLK_CTR);
	return sprintf(buf, "%u\n", counter>>ADC_CLK_CTR_SRC_SHL);
}

static ssize_t store_clk_counter_src(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	u32 clk_counter_src;
	if (sscanf(buf, "%u", &clk_counter_src) == 1){
		clk_counter_src &= ADC_CLK_CTR_SRC_MASK;
		acq400wr32(acq400_devices[dev->id], ADC_CLK_CTR,
				clk_counter_src << ADC_CLK_CTR_SRC_SHL);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(clk_counter_src,
		S_IRUGO|S_IWUGO, show_clk_counter_src, store_clk_counter_src);


static ssize_t show_hi_res_mode(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	u32 mode = acq400rd32(adev, ACQ435_MODE);
	return sprintf(buf, "%u\n", (mode&ACQ435_MODE_HIRES_512)? 1: 0);
}

static ssize_t store_hi_res_mode(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	u32 hi_res_mode;
	if (sscanf(buf, "%u", &hi_res_mode) == 1){
		u32 mode = acq400rd32(adev, ACQ435_MODE);
		if (hi_res_mode == 0){
			mode &= ~ACQ435_MODE_HIRES_512;
		}else{
			mode |= ACQ435_MODE_HIRES_512;
		}
		hi_res_mode &= ADC_CLK_CTR_SRC_MASK;
		acq400wr32(adev, ACQ435_MODE, mode);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(hi_res_mode,
		S_IRUGO|S_IWUGO, show_hi_res_mode, store_hi_res_mode);

/** NB inverted to 1: enabled */

#define BADBANKMASK 0xffffffff

unsigned bank_mask_txt2int(const char* txt)
{
	const char* cursor;
	unsigned mask = 0;

	for (cursor = txt; *cursor && cursor-txt < 4; ++cursor){
		switch(*cursor){
		case 'A':	mask |= 1<<0; break;
		case 'B':	mask |= 1<<1; break;
		case 'C':	mask |= 1<<2; break;
		case 'D':	mask |= 1<<3; break;
		default:	return BADBANKMASK;
		}
	}
	return mask;
}

char* bank_mask_int2txt(unsigned mask, char txt[])
{
	char* pt = txt;
	int bit = 0;
	for (bit = 0; bit < 4; ++bit, mask >>= 1){
		if ((mask&1) != 0){
			switch(bit){
			case 0:		*pt++ = 'A'; break;
			case 1:		*pt++ = 'B'; break;
			case 2:		*pt++ = 'C'; break;
			case 3:		*pt++ = 'D'; break;
			}
		}
	}
	*pt++ ='\0';
	return txt;
}
static ssize_t show_bank_mask(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	u32 mask = ~acq400rd32(adev, ACQ435_MODE) & ACQ435_MODE_BXDIS;
	char txt[8];
	return sprintf(buf, "%s\n",  bank_mask_int2txt(mask, txt));
}

unsigned nbits(unsigned mask)
{
	unsigned nb;
	for(nb = 0; mask != 0; mask >>= 1){
		if (mask&1){
			++nb;
		}
	}

	return nb;
}

static ssize_t store_bank_mask(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
/* user mask: 1=enabled. Compute nchan_enabled BEFORE inverting MASK */
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	u32 bank_mask = bank_mask_txt2int(buf);
	if (bank_mask != BADBANKMASK){
		u32 mode = acq400rd32(adev, ACQ435_MODE);

		mode &= ~ACQ435_MODE_BXDIS;
		adev->nchan_enabled = 8 * nbits(bank_mask);
		bank_mask = ~bank_mask&ACQ435_MODE_BXDIS;
		mode |= bank_mask;

		acq400wr32(adev, ACQ435_MODE, mode);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(bank_mask,
		S_IRUGO|S_IWUGO, show_bank_mask, store_bank_mask);


static ssize_t show_active_chan(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%d\n", adev->nchan_enabled);
}

static DEVICE_ATTR(active_chan,
		S_IRUGO|S_IWUGO, show_active_chan, 0);

static ssize_t show_module_type(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->mod_id>>MOD_ID_TYPE_SHL);
}


static DEVICE_ATTR(module_type, S_IRUGO, show_module_type, 0);


static ssize_t show_module_name(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	const char* name;

	switch(adev->mod_id>>MOD_ID_TYPE_SHL){
	case MOD_ID_ACQ1001SC:
		name = "acq1001sc"; break;
	case MOD_ID_ACQ2006SC:
		name = "acq2006sc"; break;
	case MOD_ID_ACQ420FMC:
		name = "acq420fmc"; break;
	case MOD_ID_ACQ435ELF:
		name = "acq435elf"; break;
	case MOD_ID_ACQ430FMC:
		name = "acq430fmc"; break;
	case MOD_ID_ACQ440FMC:
		name = "acq440fmc"; break;
	case MOD_ID_ACQ425ELF:
		name = "acq425elf"; break;
	case MOD_ID_AO420FMC:
		name = "ao420fmc"; break;
	case MOD_ID_AO421FMC:
		name = "ao421fmc"; break;
	default:
		name = "unknown";
		break;
	}
	return sprintf(buf, "%s\n", name);
}


static DEVICE_ATTR(module_name, S_IRUGO, show_module_name, 0);

static ssize_t show_site(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->of_prams.site);
}


static DEVICE_ATTR(site, S_IRUGO, show_site, 0);

static ssize_t show_sysclkhz(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->sysclkhz);
}


static DEVICE_ATTR(sysclkhz, S_IRUGO, show_sysclkhz, 0);

static const struct attribute *sysfs_base_attrs[] = {
	&dev_attr_module_type.attr,
	&dev_attr_module_name.attr,
	&dev_attr_nbuffers.attr,
	&dev_attr_bufferlen.attr,
	&dev_attr_site.attr,
	&dev_attr_data32.attr,
	&dev_attr_spad.attr,
	&dev_attr_spad0.attr,
	&dev_attr_spad1.attr,
	&dev_attr_spad2.attr,
	&dev_attr_spad3.attr,
	&dev_attr_spad4.attr,
	&dev_attr_spad5.attr,
	&dev_attr_spad6.attr,
	&dev_attr_spad7.attr,
	NULL
};

static const struct attribute *sysfs_device_attrs[] = {
	&dev_attr_clkdiv.attr,
	&dev_attr_simulate.attr,
	&dev_attr_stats.attr,
	&dev_attr_event1.attr,
	&dev_attr_event0.attr,
	&dev_attr_sync.attr,
	&dev_attr_trg.attr,
	&dev_attr_clk.attr,
	&dev_attr_clk_count.attr,
	&dev_attr_clk_counter_src.attr,
	&dev_attr_sample_count.attr,
	&dev_attr_shot.attr,
	&dev_attr_run.attr,
	&dev_attr_hitide.attr,
	&dev_attr_lotide.attr,
	&dev_attr_sysclkhz.attr,
	&dev_attr_active_chan.attr,
	NULL,
};

static const struct attribute *acq420_attrs[] = {
	&dev_attr_gains.attr,
	&dev_attr_gain1.attr,
	&dev_attr_gain2.attr,
	&dev_attr_gain3.attr,
	&dev_attr_gain4.attr,

	NULL
};
static const struct attribute *acq435_attrs[] = {
	&dev_attr_hi_res_mode.attr,
	&dev_attr_bank_mask.attr,
	NULL
};

static ssize_t show_dac_range(
	int chan,
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	u32 ranges = acq400rd32(acq400_devices[dev->id], AO420_RANGE);

	return sprintf(buf, "%u\n", ranges>>chan & 1);
}

static ssize_t store_dac_range(
		int shl,
		struct device * dev,
		struct device_attribute *attr,
		const char * buf,
		size_t count)
{
	int gx;

	if (sscanf(buf, "%d", &gx) == 1){
		u32 ranges = acq400rd32(acq400_devices[dev->id], AO420_RANGE);
		unsigned bit = 1 << shl;
		if (gx){
			ranges |= bit;
		}else{
			ranges &= ~bit;
		}

		dev_dbg(dev, "set gain: %02x", ranges);
		acq400wr32(acq400_devices[dev->id], AO420_RANGE, ranges);
		return count;
	}else{
		dev_warn(dev, "rejecting input args != 4");
		return -1;
	}
}

#define MAKE_DAC_RANGE(NAME, SHL)					\
static ssize_t show_dac_range##NAME(					\
	struct device * dev,						\
	struct device_attribute *attr,					\
	char * buf)							\
{									\
	return show_dac_range(SHL, dev, attr, buf);			\
}									\
									\
static ssize_t store_dac_range##NAME(					\
	struct device * dev,						\
	struct device_attribute *attr,					\
	const char * buf,						\
	size_t count)							\
{									\
	return store_dac_range(SHL, dev, attr, buf, count);		\
}									\
static DEVICE_ATTR(dac_range_##NAME, S_IRUGO|S_IWUGO, 			\
		show_dac_range##NAME, store_dac_range##NAME)

MAKE_DAC_RANGE(01,  ao420_physChan(1));
MAKE_DAC_RANGE(02,  ao420_physChan(2));
MAKE_DAC_RANGE(03,  ao420_physChan(3));
MAKE_DAC_RANGE(04,  ao420_physChan(4));
MAKE_DAC_RANGE(REF, 4);

static void ao420_flushImmediate(struct acq400_dev *adev)
{
	unsigned *src = adev->AO_immediate._u.lw;
	void *fifo = adev->dev_virtaddr + AXI_FIFO;
	int imax = AO_CHAN/2;
	int ii = 0;

	for (ii = 0; ii < imax; ++ii){
		dev_dbg(DEVP(adev), "fifo write: %p = 0x%08x\n",
				fifo + ii*sizeof(unsigned), src[ii]);
		iowrite32(src[ii], fifo + ii*sizeof(unsigned));
	}
}

static ssize_t show_dac_immediate(
	int chan,
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	short chx = adev->AO_immediate._u.ch[chan = ao420_physChan(chan)];

	return sprintf(buf, "0x%04x %d\n", chx, chx);
}

static ssize_t store_dac_immediate(
		int chan,
		struct device * dev,
		struct device_attribute *attr,
		const char * buf,
		size_t count)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	int chx;

	chan = ao420_physChan(chan);
	if (sscanf(buf, "0x%x", &chx) == 1 || sscanf(buf, "%d", &chx) == 1){
		unsigned cr = acq400rd32(adev, DAC_CTRL);
		adev->AO_immediate._u.ch[chan] = chx;

		if ((cr&DAC_CTRL_LL) == 0){
			cr |= DAC_CTRL_LL|ADC_CTRL_ENABLE_ALL;
		}
		acq400wr32(adev, DAC_CTRL, cr);
		ao420_flushImmediate(adev);
		return count;
	}else{
		dev_warn(dev, "rejecting input args != 0x%%04x or %%d");
		return -1;
	}
}

#define MAKE_DAC_IMMEDIATE(NAME, CH)					\
static ssize_t show_dac_immediate_##NAME(				\
	struct device * dev,						\
	struct device_attribute *attr,					\
	char * buf)							\
{									\
	return show_dac_immediate(CH, dev, attr, buf);			\
}									\
									\
static ssize_t store_dac_immediate_##NAME(				\
	struct device * dev,						\
	struct device_attribute *attr,					\
	const char * buf,						\
	size_t count)							\
{									\
	return store_dac_immediate(CH, dev, attr, buf, count);		\
}									\
static DEVICE_ATTR(AO_##NAME, S_IRUGO|S_IWUGO, 			\
		show_dac_immediate_##NAME, store_dac_immediate_##NAME)

MAKE_DAC_IMMEDIATE(01, 1);
MAKE_DAC_IMMEDIATE(02, 2);
MAKE_DAC_IMMEDIATE(03, 3);
MAKE_DAC_IMMEDIATE(04, 4);


static ssize_t show_playloop_length(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->AO_playloop.length);
}

static ssize_t store_playloop_length(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	if (sscanf(buf, "%u", &adev->AO_playloop.length) == 1){
		ao420_reset_playloop(adev);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(playloop_length,
		S_IRUGO|S_IWUGO, show_playloop_length, store_playloop_length);

static ssize_t show_playloop_cursor(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	return sprintf(buf, "%u\n", adev->AO_playloop.cursor);
}

static ssize_t store_playloop_cursor(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	if (sscanf(buf, "%u", &adev->AO_playloop.cursor) == 1){
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(playloop_cursor,
		S_IRUGO|S_IWUGO, show_playloop_cursor, store_playloop_cursor);


#define TIMEOUT 1000

static int poll_dacspi_complete(struct acq400_dev *adev, u32 wv)
{
	unsigned pollcat = 0;
	while( ++pollcat < TIMEOUT){
		u32 rv = acq400rd32(adev, AO420_DACSPI);
		if ((rv&AO420_DACSPI_WC) != 0){
			dev_dbg(DEVP(adev),
			"poll_dacspi_complete() success after %d\n", pollcat);
			wv &= ~(AO420_DACSPI_CW|AO420_DACSPI_WC);
			acq400wr32(adev, AO420_DACSPI, wv);
			return 0;
		}
		if ((pollcat%100)==0){
			dev_warn(DEVP(adev),
					"poll_dacspi_complete %d %08x %08x\n",
					pollcat, wv, rv);
		}
	}
	dev_err(DEVP(adev), "poll_dacspi_complete() giving up, no completion");
	return -1;
}
static ssize_t show_dacspi(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	u32 dacspi = acq400rd32(adev, AO420_DACSPI);

	return sprintf(buf, "0x%08x\n", dacspi);
}

static ssize_t store_dacspi(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	unsigned dacspi;
	if (sscanf(buf, "0x%x", &dacspi) == 1 || sscanf(buf, "%d", &dacspi) == 1){
		dacspi |= AO420_DACSPI_CW;
		acq400wr32(adev, AO420_DACSPI, dacspi);

		if ((dacspi&AO420_DACSPI_CW) != 0){
			if (poll_dacspi_complete(adev, dacspi)){
				return -1;
			}
		}
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(dacspi, S_IWUGO, show_dacspi, store_dacspi);

static ssize_t show_dacreset(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	unsigned dac_ctrl = acq400rd32(adev, DAC_CTRL);

	return sprintf(buf, "%u\n", (dac_ctrl&ADC_CTRL_ADC_RST) != 0);
}

static ssize_t store_dacreset(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	unsigned dac_ctrl = acq400rd32(adev, DAC_CTRL);
	unsigned dacreset;

	dac_ctrl |= ADC_CTRL_MODULE_EN;

	if (sscanf(buf, "%d", &dacreset) == 1){
		if (dacreset){
			dac_ctrl |= ADC_CTRL_ADC_RST;
		}else{
			dac_ctrl &= ~ADC_CTRL_ADC_RST;
		}
		acq400wr32(adev, DAC_CTRL, dac_ctrl);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(dacreset, S_IWUGO, show_dacreset, store_dacreset);

static const struct attribute *ao420_attrs[] = {
	&dev_attr_dac_range_01.attr,
	&dev_attr_dac_range_02.attr,
	&dev_attr_dac_range_03.attr,
	&dev_attr_dac_range_04.attr,
	&dev_attr_dac_range_REF.attr,
	&dev_attr_AO_01.attr,
	&dev_attr_AO_02.attr,
	&dev_attr_AO_03.attr,
	&dev_attr_AO_04.attr,
	&dev_attr_playloop_length.attr,
	&dev_attr_playloop_cursor.attr,
	&dev_attr_dacspi.attr,
	&dev_attr_dacreset.attr,
	NULL
};

#define SCOUNT_KNOB(name, reg) 						\
static ssize_t show_clk_count_##name(					\
	struct device * dev,						\
	struct device_attribute *attr,					\
	char * buf)							\
{									\
	u32 counter = acq400rd32_upcount(acq400_devices[dev->id], reg);	\
	return sprintf(buf, "%u\n", counter);				\
}									\
static DEVICE_ATTR(scount_##name, S_IRUGO|S_IWUGO, show_clk_count_##name, 0)

SCOUNT_KNOB(CLK_EXT, 	ACQ2006_CLK_COUNT(0));
SCOUNT_KNOB(CLK_MB, 	ACQ2006_CLK_COUNT(1));
SCOUNT_KNOB(CLK_S1,     ACQ2006_CLK_COUNT(SITE2DX(1)));
SCOUNT_KNOB(CLK_S2,     ACQ2006_CLK_COUNT(SITE2DX(2)));
SCOUNT_KNOB(CLK_S3,     ACQ2006_CLK_COUNT(SITE2DX(3)));
SCOUNT_KNOB(CLK_S4,     ACQ2006_CLK_COUNT(SITE2DX(4)));
SCOUNT_KNOB(CLK_S5,     ACQ2006_CLK_COUNT(SITE2DX(5)));
SCOUNT_KNOB(CLK_S6,     ACQ2006_CLK_COUNT(SITE2DX(6)));

SCOUNT_KNOB(TRG_EXT, 	ACQ2006_TRG_COUNT(0));
SCOUNT_KNOB(TRG_MB, 	ACQ2006_TRG_COUNT(1));
SCOUNT_KNOB(TRG_S1,     ACQ2006_TRG_COUNT(SITE2DX(1)));
SCOUNT_KNOB(TRG_S2,     ACQ2006_TRG_COUNT(SITE2DX(2)));
SCOUNT_KNOB(TRG_S3,     ACQ2006_TRG_COUNT(SITE2DX(3)));
SCOUNT_KNOB(TRG_S4,     ACQ2006_TRG_COUNT(SITE2DX(4)));
SCOUNT_KNOB(TRG_S5,     ACQ2006_TRG_COUNT(SITE2DX(5)));
SCOUNT_KNOB(TRG_S6,     ACQ2006_TRG_COUNT(SITE2DX(6)));

SCOUNT_KNOB(EVT_EXT, 	ACQ2006_EVT_COUNT(0));
SCOUNT_KNOB(EVT_MB, 	ACQ2006_EVT_COUNT(1));
SCOUNT_KNOB(EVT_S1,     ACQ2006_EVT_COUNT(SITE2DX(1)));
SCOUNT_KNOB(EVT_S2,     ACQ2006_EVT_COUNT(SITE2DX(2)));
SCOUNT_KNOB(EVT_S3,     ACQ2006_EVT_COUNT(SITE2DX(3)));
SCOUNT_KNOB(EVT_S4,     ACQ2006_EVT_COUNT(SITE2DX(4)));
SCOUNT_KNOB(EVT_S5,     ACQ2006_EVT_COUNT(SITE2DX(5)));
SCOUNT_KNOB(EVT_S6,     ACQ2006_EVT_COUNT(SITE2DX(6)));

SCOUNT_KNOB(SYN_EXT, 	ACQ2006_SYN_COUNT(0));
SCOUNT_KNOB(SYN_MB, 	ACQ2006_SYN_COUNT(1));
SCOUNT_KNOB(SYN_S1,     ACQ2006_SYN_COUNT(SITE2DX(1)));
SCOUNT_KNOB(SYN_S2,     ACQ2006_SYN_COUNT(SITE2DX(2)));
SCOUNT_KNOB(SYN_S3,     ACQ2006_SYN_COUNT(SITE2DX(3)));
SCOUNT_KNOB(SYN_S4,     ACQ2006_SYN_COUNT(SITE2DX(4)));
SCOUNT_KNOB(SYN_S5,     ACQ2006_SYN_COUNT(SITE2DX(5)));
SCOUNT_KNOB(SYN_S6,     ACQ2006_SYN_COUNT(SITE2DX(6)));


static ssize_t show_fan_percent(
	struct device * dev,
	struct device_attribute *attr,
	char * buf)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	unsigned pwm = acq400rd32(adev, MOD_CON)&ACQ1001_MOD_CON_PWM_MASK;
	unsigned fan_percent;

	/* scale to 1..100 */
	pwm >>= ACQ1001_MOD_CON_PWM_BIT;
	if (pwm > ACQ1001_MOD_CON_PWM_MIN){
		pwm -= ACQ1001_MOD_CON_PWM_MIN;
	}
	fan_percent = (pwm*2)/5;
	return sprintf(buf, "%u\n", fan_percent);
}

static ssize_t store_fan_percent(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	unsigned mod_con = acq400rd32(adev, MOD_CON);
	unsigned fan_percent;

	if (sscanf(buf, "%d", &fan_percent) == 1){
		unsigned pwm = (fan_percent*5)/2;
		pwm += ACQ1001_MOD_CON_PWM_MIN;
		pwm <<= ACQ1001_MOD_CON_PWM_BIT;
		if (pwm > ACQ1001_MOD_CON_PWM_MASK){
			pwm = ACQ1001_MOD_CON_PWM_MASK;
		}else{
			pwm &= ACQ1001_MOD_CON_PWM_MASK;
		}
		mod_con &= ~ACQ1001_MOD_CON_PWM_MASK;
		mod_con |= pwm;

		dev_info(DEVP(adev), "store_fan_percent:%d write %08x\n",
				fan_percent, mod_con);

		acq400wr32(adev, MOD_CON, mod_con);
		return count;
	}else{
		return -1;
	}
}

static DEVICE_ATTR(fan_percent, S_IWUGO|S_IRUGO, show_fan_percent, store_fan_percent);

static ssize_t show_acq0000_mod_con(
	struct device * dev,
	struct device_attribute *attr,
	char * buf,
	unsigned mask)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	u32 mod_con = acq400rd32(adev, MOD_CON);

	return sprintf(buf, "%u\n", (mod_con&mask) != 0);
}



static ssize_t store_acq0000_mod_con(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count,
	unsigned mask)
{
	unsigned enable;
	if (sscanf(buf, "%u", &enable) == 1){
		struct acq400_dev *adev = acq400_devices[dev->id];
		u32 mod_con = acq400rd32(adev, MOD_CON);

		if (enable){
			mod_con |= mask;
		}else{
			mod_con &= ~mask;
		}
		acq400wr32(adev, MOD_CON, mod_con);
		return count;
	}else{
		return -1;
	}
}

#define MODCON_KNOB(name, mask)						\
static ssize_t show_##name(						\
	struct device * dev,						\
	struct device_attribute *attr,					\
	char * buf)							\
{									\
	return show_acq0000_mod_con(dev, attr, buf, mask);		\
}									\
static ssize_t store_##name(						\
	struct device * dev,						\
	struct device_attribute *attr,					\
	const char * buf,						\
	size_t count)							\
{									\
	return store_acq0000_mod_con(dev, attr, buf, count, mask);	\
}									\
static DEVICE_ATTR(name, S_IRUGO|S_IWUGO, show_##name, store_##name)

MODCON_KNOB(mod_en, 	ACQ1001_MOD_CON_MOD_EN);
MODCON_KNOB(psu_sync, 	ACQ1001_MOD_CON_PSU_SYNC);
MODCON_KNOB(fan,	ACQ1001_MOD_CON_FAN_EN);
MODCON_KNOB(soft_trig,  MOD_CON_SOFT_TRIG);

int get_agg_threshold_bytes(struct acq400_dev *adev, u32 agg)
{
	return ((agg>>AGG_SIZE_SHL)&AGG_SIZE_MASK)*AGG_SIZE_UNIT(adev);
}

#define AGG_SEL	"aggregator="
#define TH_SEL	"threshold="

static ssize_t show_reg(
	struct device * dev,
	struct device_attribute *attr,
	char * buf,
	const unsigned offset,
	const unsigned mshift)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	u32 regval = acq400rd32(adev, offset);
	char mod_group[80];
	int site;

	for (site = 1, mod_group[0] = '\0'; site <= 6; ++site){
		if ((regval & AGG_MOD_EN(site, mshift)) != 0){
			if (strlen(mod_group) == 0){
				strcat(mod_group, "sites=");
			}else{
				strcat(mod_group, ",");
			}
			sprintf(mod_group+strlen(mod_group), "%d", site);
		}
	}
	if (strlen(mod_group) == 0){
		sprintf(mod_group, "sites=none");
	}
	if (mshift == DATA_ENGINE_MSHIFT){
		sprintf(mod_group+strlen(mod_group), " %s%d", AGG_SEL,
				regval&DATA_ENGINE_SELECT_AGG? 1: 0);
	}else if(mshift == AGGREGATOR_MSHIFT){
		sprintf(mod_group+strlen(mod_group), " %s%d", TH_SEL,
				get_agg_threshold_bytes(adev, regval));
	}

	return sprintf(buf, "0x%08x %s %s\n", regval, mod_group,
			regval&DATA_MOVER_EN? "on": "off");
}

int _get_site(const char* valid_sites, char s)
{
	if (strchr(valid_sites, s) != 0){
		return s-'0';
	}else{
		return -1;
	}
}
int get_site(struct acq400_dev *adev, char s)
/** @todo cant discriminate 1001, 1002... */
{
	if (IS_ACQ2006SC(adev)){
		return _get_site("123456", s);
	}else if (IS_ACQ1001SC(adev)){
		return _get_site("12", s);
	}else{
		return -2;
	}
}
static ssize_t store_reg(
	struct device * dev,
	struct device_attribute *attr,
	const char * buf,
	size_t count,
	const unsigned offset,
	const unsigned mshift)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	unsigned regval;
	char* match;
	int pass = 0;

	if ((match = strstr(buf, "sites=")) != 0){
		unsigned regval = acq400rd32(adev, offset);
		char* cursor = match+strlen("sites=");
		int site;

		regval &= ~(AGG_SITES_MASK << mshift);

		if (strncmp(cursor, "none", 4) != 0){
			for (; *cursor && *cursor != ' '; ++cursor){
				switch(*cursor){
				case ',':
				case ' ':	continue;
				case '\n':  	break;
				default:
					site = get_site(adev, *cursor);
					if (site > 0){
						regval |= AGG_MOD_EN(site, mshift);
						break;
					}else{
						dev_err(dev, "bad site designator: %c", *cursor);
						return -1;
					}
				}
			}
		}
		acq400wr32(adev, offset, regval);
		pass = 1;
	}
	if (mshift == DATA_ENGINE_MSHIFT){
		char *agg_sel = strstr(buf, AGG_SEL);
		if (agg_sel){
			int include_agg = 0;
			if (sscanf(agg_sel, AGG_SEL"%d", &include_agg) == 1){
				unsigned regval = acq400rd32(adev, offset);
				if (include_agg){
					regval |= DATA_ENGINE_SELECT_AGG;
				}else{
					regval &= ~DATA_ENGINE_SELECT_AGG;
				}
				acq400wr32(adev, offset, regval);
				pass = 1;
			}
		}
	}else if (mshift == AGGREGATOR_MSHIFT){
		char *th_sel = strstr(buf, TH_SEL);
		if (th_sel){
			int thbytes = 0;
			if (sscanf(th_sel, TH_SEL"%d", &thbytes) == 1){
				unsigned regval = acq400rd32(adev, offset);
				unsigned th = thbytes/AGG_SIZE_UNIT(adev);
				if (th > AGG_SIZE_MASK) th = AGG_SIZE_MASK;
				regval &= ~(AGG_SIZE_MASK<<AGG_SIZE_SHL);
				regval |= th<<AGG_SIZE_SHL;
				acq400wr32(adev, offset, regval);
				pass = 1;
			}else{
				dev_err(dev, "arg not integer (%s)", th_sel);
				return -1;
			}
		}
	}
	if ((match = strstr(buf, "on")) != 0){
		unsigned regval = acq400rd32(adev, offset);
		regval |= DATA_MOVER_EN;
		acq400wr32(adev, offset, regval);
		pass = 1;
	}else if ((match = strstr(buf, "off")) != 0){
		unsigned regval = acq400rd32(adev, offset);
		regval &= ~DATA_MOVER_EN;
		acq400wr32(adev, offset, regval);
		pass = 1;
	}

	if (pass){
		return count;
	}else{
		/* aggregator= passes this paragraph, so put it LAST! */
		if (sscanf(buf, "%x", &regval) == 1){
			acq400wr32(adev, offset, regval);
			return count;
		}
		return -1;
	}
}


#define REG_KNOB(name, offset, mshift)					\
static ssize_t show_reg_##name(						\
	struct device * dev,						\
	struct device_attribute *attr,					\
	char * buf)							\
{									\
	return show_reg(dev, attr, buf, offset, mshift);		\
}									\
static ssize_t store_reg_##name(					\
	struct device * dev,						\
	struct device_attribute *attr,					\
	const char * buf,						\
	size_t count)							\
{									\
	return store_reg(dev, attr, buf, count,  offset, mshift);	\
}									\
static DEVICE_ATTR(name, 						\
	S_IRUGO|S_IWUGO, show_reg_##name, store_reg_##name)

REG_KNOB(aggregator, AGGREGATOR,	AGGREGATOR_MSHIFT);
REG_KNOB(data_engine_0, DATA_ENGINE(0), DATA_ENGINE_MSHIFT);
REG_KNOB(data_engine_1, DATA_ENGINE(1), DATA_ENGINE_MSHIFT);
REG_KNOB(data_engine_2, DATA_ENGINE(2), DATA_ENGINE_MSHIFT);
REG_KNOB(data_engine_3, DATA_ENGINE(3), DATA_ENGINE_MSHIFT);



static ssize_t show_aggsta(
	struct device * dev,
	struct device_attribute *attr,
	char * buf,
	unsigned mask)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	unsigned aggsta = acq400rd32(adev, AGGSTA);
	unsigned field = aggsta&mask;

	for (; (mask&0x1) == 0; mask >>=1, field >>= 1){
		;
	}

	return sprintf(buf, "%u\n", field);
}

#define SHOW_AGGSTA(name, mask) 					\
static ssize_t show_aggsta_##name(					\
	struct device *dev,						\
	struct device_attribute *attr,					\
	char* buf )							\
{									\
	return show_aggsta(dev, attr, buf, mask);			\
}									\
static DEVICE_ATTR(name, S_IRUGO, show_aggsta_##name, 0)

SHOW_AGGSTA(aggsta_fifo_count, AGGSTA_FIFO_COUNT);
SHOW_AGGSTA(aggsta_fifo_stat,  AGGSTA_FIFO_STAT);
SHOW_AGGSTA(aggsta_engine_stat, AGGSTA_ENGINE_STAT);

static const struct attribute *sc_common_attrs[] = {
	&dev_attr_aggregator.attr,
	&dev_attr_aggsta_fifo_count.attr,
	&dev_attr_aggsta_fifo_stat.attr,
	&dev_attr_aggsta_engine_stat.attr,
	&dev_attr_mod_en.attr,
	&dev_attr_psu_sync.attr,
	&dev_attr_soft_trig.attr,
	&dev_attr_gpg_top.attr,
	&dev_attr_gpg_trg.attr,
	&dev_attr_gpg_clk.attr,
	&dev_attr_gpg_sync.attr,
	&dev_attr_gpg_mode.attr,
	&dev_attr_gpg_enable.attr,
	NULL
};
static const struct attribute *acq2006sc_attrs[] = {
	&dev_attr_data_engine_0.attr,
	&dev_attr_data_engine_1.attr,
	&dev_attr_data_engine_2.attr,
	&dev_attr_data_engine_3.attr,

	&dev_attr_scount_CLK_EXT.attr,
	&dev_attr_scount_CLK_MB.attr,
	&dev_attr_scount_CLK_S1.attr,
	&dev_attr_scount_CLK_S2.attr,
	&dev_attr_scount_CLK_S3.attr,
	&dev_attr_scount_CLK_S4.attr,
	&dev_attr_scount_CLK_S5.attr,
	&dev_attr_scount_CLK_S6.attr,

	&dev_attr_scount_TRG_EXT.attr,
	&dev_attr_scount_TRG_MB.attr,
	&dev_attr_scount_TRG_S1.attr,
	&dev_attr_scount_TRG_S2.attr,
	&dev_attr_scount_TRG_S3.attr,
	&dev_attr_scount_TRG_S4.attr,
	&dev_attr_scount_TRG_S5.attr,
	&dev_attr_scount_TRG_S6.attr,

	&dev_attr_scount_EVT_EXT.attr,
	&dev_attr_scount_EVT_MB.attr,
	&dev_attr_scount_EVT_S1.attr,
	&dev_attr_scount_EVT_S2.attr,
	&dev_attr_scount_EVT_S3.attr,
	&dev_attr_scount_EVT_S4.attr,
	&dev_attr_scount_EVT_S5.attr,
	&dev_attr_scount_EVT_S6.attr,

	&dev_attr_scount_SYN_EXT.attr,
	&dev_attr_scount_SYN_MB.attr,
	&dev_attr_scount_SYN_S1.attr,
	&dev_attr_scount_SYN_S2.attr,
	&dev_attr_scount_SYN_S3.attr,
	&dev_attr_scount_SYN_S4.attr,
	&dev_attr_scount_SYN_S5.attr,
	&dev_attr_scount_SYN_S6.attr,
	NULL
};

static const struct attribute *acq1001sc_attrs[] = {
	&dev_attr_data_engine_0.attr,
	&dev_attr_fan.attr,
	&dev_attr_fan_percent.attr,

	&dev_attr_scount_CLK_EXT.attr,
	&dev_attr_scount_CLK_MB.attr,
	&dev_attr_scount_CLK_S1.attr,
	&dev_attr_scount_CLK_S2.attr,

	&dev_attr_scount_TRG_EXT.attr,
	&dev_attr_scount_TRG_MB.attr,
	&dev_attr_scount_TRG_S1.attr,
	&dev_attr_scount_TRG_S2.attr,

	&dev_attr_scount_EVT_EXT.attr,
	&dev_attr_scount_EVT_MB.attr,
	&dev_attr_scount_EVT_S1.attr,
	&dev_attr_scount_EVT_S2.attr,

	&dev_attr_scount_SYN_EXT.attr,
	&dev_attr_scount_SYN_MB.attr,
	&dev_attr_scount_SYN_S1.attr,
	&dev_attr_scount_SYN_S2.attr,
	NULL
};
void acq400_createSysfs(struct device *dev)
{
	struct acq400_dev *adev = acq400_devices[dev->id];
	const struct attribute **specials = 0;

	dev_info(dev, "acq400_createSysfs()");
	if (sysfs_create_files(&dev->kobj, sysfs_base_attrs)){
		dev_err(dev, "failed to create sysfs");
	}

	if (IS_DUMMY(adev)){
		return;
	}else if IS_ACQx00xSC(adev){
		if (sysfs_create_files(&dev->kobj, sc_common_attrs)){
			dev_err(dev, "failed to create sysfs");
		}
		if (IS_ACQ2006SC(adev)){
			specials = acq2006sc_attrs;
		}else if (IS_ACQ1001SC(adev)){
			specials = acq1001sc_attrs;
		}
	}else{
		if (sysfs_create_files(&dev->kobj, sysfs_device_attrs)){
			dev_err(dev, "failed to create sysfs");
		}
		if (IS_ACQ420(adev)){
			specials = acq420_attrs;
		}else if (IS_ACQ43X(adev)){
			specials = acq435_attrs;
		}else if (IS_AO420(adev)){
			specials = ao420_attrs;
		}else{
			return;
		}
	}


	if (sysfs_create_files(&dev->kobj, specials)){
		dev_err(dev, "failed to create sysfs");
	}
}

void acq400_delSysfs(struct device *dev)
{
	sysfs_remove_files(&dev->kobj, sysfs_base_attrs);
	// @@todo .. undoing the rest will be interesting .. */
}


