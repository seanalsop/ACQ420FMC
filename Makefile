# Cross compiler makefile for FIFO DMA example
KERN_SRC=~/PROJECTS/ACQ400/linux-xlnx
obj-m += acq420fmc.o
obj-m += dmatest_pgm.o
obj-m += debugfs2.o
obj-m += pl330_fs.o
obj-m += pl330.o
obj-m += bolo8_drv.o
obj-m += acq425_drv.o
obj-m += acq480.o
obj-m += acq400t.o
#obj-m += acq4xx_fs.o
obj-m += mgt400.o
obj-m += regfs.o
obj-m += acq400_dspfs.o
obj-m += xilinx_axidma.o
obj-m += pigcelf.o
obj-m += dmadescfs.o
obj-m += radcelf.o
obj-m += ad9854.o
obj-m += ad9512.o
obj-m += fmc10x.o
obj-m += ad9510.o
obj-m += ads62p49.o
obj-m += ao428.o
obj-m += lp3943_dev.o
#obj-m += acq400-spi-bytebang.o

DC=$(shell date +%y%m%d%H%M%S)
SEQ=10


CPPFLAGS += -O3 -Wall

acq420fmc-objs := acq400_drv.o  acq400_ui.o acq400_fs.o \
	acq400_core.o acq400_init_defaults.o \
	acq400_debugfs.o acq400_sysfs.o acq400_lists.o \
	acq400_proc.o hbm.o zynq-timer.o \
	bolo8_core.o bolo_ui.o bolo8_sysfs.o \
	dio432_drv.o  ao424_drv.o acq400_sewfifo.o \
	acq423_sysfs.o 	acq480_sysfs.o v2f_sysfs.o \
	acq400_xilinx_axidma.o acq400_deltrg.o \
	acq400_set.o acq400_sysfs_utils.o  \
	acq400_axi_chain.o acq400_axi_oneshot.o \
	radcelf_sysfs.o acq400_reg_cache.o
	
dmadescfs-objs := dmadescfs_drv.o

regfs-objs := regfs_drv.o
	

mgt400-objs := mgt400_drv.o mgt400_sysfs.o mgt400_procfs.o mgt400_debugfs.o \
 		acq400_reg_cache.o

acq480-objs := acq480_drv.o hbm.o zynq_peripheral_spi_shim.o

pigcelf-objs := pigcelf_drv.o zynq_peripheral_spi_shim.o

radcelf-objs := radcelf_drv.o zynq_peripheral_spi_shim.o

fmc10x-objs := fmc10x_drv.o zynq_peripheral_spi_shim.o

ao428-objs := ao428_drv.o

acq400t-objs := acq400t_drv.o

dmatest_pgm-objs := dmatest.o zynq-timer.o

APPS := mmap acq400_stream permute acq435_decode \
	acq400_knobs udp_client is_ramp mmaptest wavegen \
	dsp_coprocessor ramp acq400_stream_disk \
	acq480_knobs transition_counter acq435_rtm_trim anatrg \
	muxdec dmadescfs_test tblock2file acq400_sls bb \
	fix_state bpaste clocks_to_first_edge \
	mgtdram_descgen bigcat egu2int

all: modules apps
	
date:
	echo $(DC)

package: all
	echo do NOT rm -Rf opkg/*
	mkdir -p opkg/usr/local/bin opkg/usr/local/lib/modules \
		opkg/usr/share opkg/usr/local/CARE opkg/usr/local/map \
		opkg/usr/local/init/pl330dbg opkg/usr/local/cal \
		opkg/etc/profile.d opkg/etc/sysconfig opkg/etc/acq400
	cp cal/* opkg/usr/local/cal
	cp -a $(APPS) scripts/* opkg/usr/local/bin
	cp *.ko opkg/usr/local/lib/modules
	cp ../linux-xlnx/drivers/hwmon/adt*ko opkg/usr/local/lib/modules
	cp -a doc opkg/usr/share
	cp bos.sh opkg/etc/profile.d
	cp acq435_decode CARE/* scripts/streamtonowhere opkg/usr/local/CARE
	cp init/* opkg/usr/local/init
	cp map/* opkg/usr/local/map
	cp pl330dbg/* opkg/usr/local/init/pl330dbg
	cp sysconfig/* opkg/etc/sysconfig
	rm -f opkg/usr/local/bin/mgt_offload
	ln -s /usr/local/CARE/mgt_offload_groups opkg/usr/local/bin/mgt_offload
	tar czf release/$(SEQ)-acq420-$(DC).tgz -C opkg .
	@echo created package release/$(SEQ)-acq420-$(DC).tgz
	rm -f ../PACKAGES/$(SEQ)-acq420*
	git tag -a -m $(SEQ)-acq420-$(DC).tgz r$(DC) 
	cp release/$(SEQ)-acq420-$(DC).tgz ../PACKAGES/

tarball:
	echo Please make sure directory is clean first ..
	tar cvzf acq420fmc-$(DC).tgz --exclude=release/* *


apps: $(APPS)

modules:
	make -C $(KERN_SRC) ARCH=arm M=`pwd` modules
	
clean:
	@rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c .tmp_versions $(APPS) \
		Module.symvers modules.order
	
mmap: mmap.o
	$(CC) -o $@ $^ -L../lib -lpopt

dsp_coprocessor: dsp_coprocessor.o
	$(CC) -o $@ $^ -L../lib -lpopt -lrt -lm

mmaptest: mmaptest.o
	$(CC) -o $@ $^ -L../lib -lpopt

acq400_sls: acq400_sls.o acq-util.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt	
		
udp_client: udp_client.o
	$(CC) -o $@ $^ -L../lib -lpopt
	
acq400_stream: acq400_stream.o Buffer.o knobs.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt -lpthread -lrt

bb: bb.o Buffer.o knobs.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt -lpthread -lrt

phased_array: phased_array.o Buffer.o knobs.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt -lpthread -lrt
	
tblock2file: tblock2file.o knobs.o acq-util.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt -lpthread -lrt

is_ramp: is_ramp.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt
	
acq400_knobs: acq400_knobs.o tcp_server.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt

anatrg: anatrg.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt

multisitecheckramp: multisitecheckramp.cpp
	$(CXX) -std=c++11 -O3 -o $@ $^ -L../lib -lpopt
	
acq480_knobs: acq480_knobs.o ads5294.o  
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt
wavegen: wavegen.o acq-util.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt -lm
	
acq435_decode: acq435_decode.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt -lpthread
	
acq400_axi_dma_test_harness: acq400_axi_dma_test_harness.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt -lpthread	

acq435_rtm_trim: acq435_rtm_trim.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt -lpthread
	
bigmac: bigmac.o
	$(CXX) -mcpu=cortex-a9 -mfloat-abi=softfp -mfpu=neon \
		-DHASNEON \
		-O3 -o bigmac bigmac.o -L../lib -lpopt
bigcat: bigcat.cpp
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt -lpthread

lilmac: lilmac.o
	$(CXX) -O3 -o lilmac lilmac.o -L../lib -lpopt

muxdec: muxdec.o acq-util.o
	$(CXX) -O3 -o muxdec muxdec.o acq-util.o

bpaste: bpaste.o
	$(CXX) -O3 -o bpaste bpaste.o
	
bigmac.x86: bigmac.o
	$(CXX) -O3 -o $@ $^ -lpopt	
	
mgtdram_descgen: 	mgtdram_descgen.o
	$(CXX) -O3 -o $@ $^ -L../lib -lpopt


rtpackage:
	tar cvzf dmadescfs-$(DC).tgz dmadescfs* scripts/load.dmadescfs

		
zynq:
	./make.zynq
		
