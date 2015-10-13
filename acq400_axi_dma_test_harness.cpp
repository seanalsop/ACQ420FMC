/*
 * acq400_axi_dma_test_harness.cpp
 *
 *  Created on: 29 Sep 2015
 *      Author: pgm
 */


#include <sched.h>

#include <stdio.h>
#include <string.h>

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <string>
#include <vector>

#include <syslog.h>

using namespace std;

int BUFFER_LEN = 1048576;

typedef unsigned u32;

struct buffer {
	char id[4];
	u32 pa;
	u32* va;
	int len;

	buffer() : pa(0), va(0), len(0) {
		strcpy(id, "-");
	}
};


struct xilinx_dma_desc_hw {
	u32 next_desc;	/* 0x00 */
	u32 pad1;	/* 0x04 */
	u32 buf_addr;	/* 0x08 */
	u32 pad2;	/* 0x0C */
	u32 pad3;	/* 0x10 */
	u32 pad4;	/* 0x14 */
	u32 control;	/* 0x18 */
	u32 status;	/* 0x1C */
	u32 app_0;	/* 0x20 */
	u32 app_1;	/* 0x24 */
	u32 app_2;	/* 0x28 */
	u32 app_3;	/* 0x2C */
	u32 app_4;	/* 0x30 */
	u32 endpad[3];
};

#define DSZ	(sizeof(struct xilinx_dma_desc_hw))

void getBuffers(vector<buffer*> &buffers)
{
	FILE* fp = fopen("/proc/driver/acq400/0/buffers", "r");
	char def[80];

	if (!fp){
		perror("/proc/driver/acq400/0/buffers");
		exit(1);
	}
	while(fgets(def, 80, fp)){
		char id[4] = {};
		unsigned pa, gash1, gash2;
		int nscan;
		int reject_count = 0;

		//printf("consider %s\n", def);

		if ((nscan = sscanf(def, "%3s,%x,%x,%d", id, &gash1, &pa, &gash2)) == 4){
			buffer *b = new buffer;
			strncpy(b->id, id, 4);
			b->pa = pa;
			b->len = BUFFER_LEN;
			buffers.push_back(b);
		}else{
			if (reject_count++){
				printf("## reject %s nscan:%d\n", def, nscan);
			}
		}
	}

	fclose(fp);
}

buffer* makeDescriptorMapping(vector<buffer*> &buffers)
{
	buffer * descriptors = buffers.back();
	buffers.pop_back();
	char fname[80];
	sprintf(fname, "%s/%s", "/dev/acq400.0.hb/", descriptors->id);
	int fd = open(fname, O_RDWR);
	if (fd < 0){
		perror(fname);
		exit(1);
	}
	descriptors->va = (unsigned*)mmap(0, BUFFER_LEN, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
	if (descriptors->va == MAP_FAILED){
		perror("mmap hb");
		exit(1);
	}
	return descriptors;
}

unsigned* makeDmacMapping()
{
	const char* fname = "/dev/mem";
	unsigned *va;
	int fd = open(fname, O_RDWR);
	if (fd < 0){
		perror(fname);
		exit(1);
	}
	va = (unsigned*)mmap(0, 0x1000, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0x801f0000);
	if (va == MAP_FAILED){
		perror("mmap /dev/mem");
		exit(1);
	}
	return va;
}

u32 makeChain(vector<buffer*> &buffers, int ndesc)
{
	buffer* descriptors = makeDescriptorMapping(buffers);
	xilinx_dma_desc_hw* hw_desc = (xilinx_dma_desc_hw*)descriptors->va;

	for (int ii = 0, lasti = ndesc - 1; ii <= lasti; ++ii){
		xilinx_dma_desc_hw hw = {};
		if (ii < lasti){
			hw.next_desc = descriptors->pa + (ii+1)*DSZ;
		}else{
			hw.next_desc = descriptors->pa;
		}
		hw.buf_addr = buffers[ii]->pa;
		hw.control = buffers[ii]->len;
		memcpy(&hw_desc[ii], &hw, sizeof(xilinx_dma_desc_hw));
	}
	return descriptors->pa;
}

void writeReg(volatile unsigned *reg, unsigned byteoff, unsigned value)
{
	reg[byteoff/sizeof(unsigned)] = value;
}

void dmacInit(volatile unsigned* dmac, unsigned chain_pa, int ndesc)
{
	unsigned tail_pa = chain_pa + (ndesc)*DSZ;
	writeReg(dmac, 0x30, 0x4);
	writeReg(dmac, 0x30, 0x0);
	writeReg(dmac, 0x38, chain_pa);
	writeReg(dmac, 0x30, 0x11);
	writeReg(dmac, 0x40, tail_pa);
}

void pollStatus(volatile unsigned* dmac, unsigned chain_pa)
{
	volatile unsigned* status = dmac+(0x30+8)/sizeof(unsigned);
	u32 s0 = *status;
	u32 s1;

	while(1){
		while ((s1 = *status) == s0){
			sched_yield();
		}
		printf("%03d\n", (s1-chain_pa)/DSZ);
		s0 = s1;
	}
}

#define BL "/sys/module/acq420fmc/parameters/bufferlen"


int main(int argc, char* argv[])
{
	int ndesc = argc>1? atoi(argv[1]): 5;

	if (argc > 2){
		BUFFER_LEN = strtoul(argv[2],0, 0);
	}
	openlog("acq400_axi_dma_test_harness", LOG_PID, LOG_USER);
	syslog(LOG_DEBUG, "%d buffers each length: %d", ndesc, BUFFER_LEN);

	//printf("size: %d\n", sizeof(struct xilinx_dma_desc_hw));
	assert(sizeof(struct xilinx_dma_desc_hw) == 64);
	vector<buffer*> buffers;
	getBuffers(buffers);

	std::vector<buffer*>::const_iterator i;
/*
	for(i=buffers.begin(); i!=buffers.end(); ++i){
		printf("%s pa 0x%08x\n", (*i)->id, (*i)->pa);
	}
*/
	u32 chain_pa = makeChain(buffers, ndesc);

	volatile unsigned *dmac = makeDmacMapping();
	dmacInit(dmac, chain_pa, ndesc);

	if (getenv("AXI_POLL_STATUS")){
		pollStatus(dmac, chain_pa);
	}
	return 0;
}