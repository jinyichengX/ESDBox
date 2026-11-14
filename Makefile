# main : binary_heap.o main.o
# 	gcc -Wall -g -o main binary_heap.o main.o

# main.o : main.c
# 	gcc -g -I ./2D/inc -c main.c -o main.o

# binary_heap.o : ./2D/src/binary_heap.c
# 	gcc -g -I ./2D/inc -c ./2D/src/binary_heap.c -o binary_heap.o

# TOP Makefile

BUILD_INFO := =============== Build OK!! Author: JinYiCheng  ===============

target = ESDBox

executable_suffix := .exe

# top Makefile directory
makefile_dir = $(CURDIR)

# debug build?
build_for_debug = 1

# build for ARM?
build_for_arm = 0

# designate gcc path when without env
# gcc_path ?= F:/GNUtools/GNUtools/gcc-arm-none-eabi-10.3-2021.10

# cross compiler prefix name
ifneq ($(build_for_arm),0)
cross_compile ?= arm-linux-gnueabihf-
endif

# Build path
build_dir = $(makefile_dir)/build

# lib path
lib_dir = lib

# input encode type GB2312 or UTF-8
input_encode = -finput-charset=UTF-8

# output encode type GB2312 or UTF-8
ouptput_encode = -fexec-charset=UTF-8

# The gcc compiler bin path can be either defined in make command via GCC_PATH variable (> make GCC_PATH=xxx)
ifdef gcc_path
CC = $(gcc_path)/$(cross_compile)gcc
CXX = $(gcc_path)/$(cross_compile)g++
#将elf文件转换成bin文件
CP = $(gcc_path)/$(cross_compile)objcopy
#反汇编
OD = $(gcc_path)/$(cross_compile)objdump
LD = $(gcc_path)/$(cross_compile)ld
AR = $(gcc_path)/$(cross_compile)ar
else
CC = $(cross_compile)gcc
CXX = $(cross_compile)g++
LD = $(cross_compile)ld
AR = $(cross_compile)ar
endif

#将问号改成冒号就出问题，why
obj_files ?= $(patsubst %.c,$(build_dir)/%.o,$(notdir $(c_srcfiles)))
obj_files += $(patsubst %.s,$(build_dir)/%.o,$(notdir $(asm_srcfiles)))

# ASM files directory
asm_dir := 

# C source files directory
csrc_dir := $(makefile_dir)/elNET/port\
			$(makefile_dir)/elNET/src\
			$(makefile_dir)/elNET/components/cjson\
			$(makefile_dir)/elNET/src/app_protocal/tftp\
			$(makefile_dir)
include_dir := $(makefile_dir)/elNET/inc\
			$(makefile_dir)/elNET/port\
			$(makefile_dir)/elNET/src\
			$(makefile_dir)/elNET/port/Include\
			$(makefile_dir)/elNET/port/Include/pcap\
			$(makefile_dir)/elNET/components/cjson\
			$(makefile_dir)/elNET/memory\
			$(makefile_dir)/elNET/src/app_protocal/tftp\
			$(makefile_dir)/elNET
#$(makefile_dir)/ESDBox_IPGUI/font/opensans
csrc_dir += $(makefile_dir)/ESDBox_IPGUI/base/src\
			$(makefile_dir)/ESDBox_IPGUI/base/src/cache\
			$(makefile_dir)/ESDBox_IPGUI/builtin_widget/src\
			$(makefile_dir)/ESDBox_IPGUI/widget/src\
			$(makefile_dir)/ESDBox_IPGUI/al/vfs\
			$(makefile_dir)/ESDBox_IPGUI/al/hal\
			$(makefile_dir)/ESDBox_IPGUI/al/hal/input\
			$(makefile_dir)/ESDBox_IPGUI/port\
			$(makefile_dir)/ESDBox_IPGUI/port/sdl\
			$(makefile_dir)/ESDBox_IPGUI/core\
			$(makefile_dir)/ESDBox_IPGUI/core/misc\
			$(makefile_dir)/ESDBox_IPGUI/charset\
			$(makefile_dir)/ESDBox_IPGUI/core/gfx\
			$(makefile_dir)/ESDBox_IPGUI/core/composite\
			$(makefile_dir)/ESDBox_IPGUI/core/composite/blend_color\
			$(makefile_dir)/ESDBox_IPGUI/core/composite/blend_gradient\
			$(makefile_dir)/ESDBox_IPGUI/core/composite/blend_image\
			$(makefile_dir)/ESDBox_IPGUI/core/image\
			$(makefile_dir)/ESDBox_IPGUI/core/image/decoder\
			$(makefile_dir)/ESDBox_IPGUI/core/image/proc\
			$(makefile_dir)/ESDBox_IPGUI/core/image/show\
			$(makefile_dir)/ESDBox_IPGUI/core/vector_render\
			$(makefile_dir)/ESDBox_IPGUI/core/ui/widget_manager\
			$(makefile_dir)/ESDBox_IPGUI/core/ui\
			$(makefile_dir)/ESDBox_IPGUI/core/ui/animation\
			$(makefile_dir)/ESDBox_IPGUI/core/ui/animation/builtin_anim\
			$(makefile_dir)/ESDBox_IPGUI/core/ui/layout\
			$(makefile_dir)/ESDBox_IPGUI/font/opensans\
			$(makefile_dir)/ESDBox_IPGUI/font/quicksand\
			$(makefile_dir)/ESDBox_IPGUI
include_dir += $(makefile_dir)/ESDBox_IPGUI/base/inc\
			$(makefile_dir)/ESDBox_IPGUI/base/src\
			$(makefile_dir)/ESDBox_IPGUI/base/inc/cache\
			$(makefile_dir)/ESDBox_IPGUI/builtin_widget/inc\
			$(makefile_dir)/ESDBox_IPGUI/widget/inc\
			$(makefile_dir)/ESDBox_IPGUI/Include\
			$(makefile_dir)/ESDBox_IPGUI/al/vfs\
			$(makefile_dir)/ESDBox_IPGUI/al/hal\
			$(makefile_dir)/ESDBox_IPGUI/al/hal/input\
			$(makefile_dir)/ESDBox_IPGUI\
			$(makefile_dir)/ESDBox_IPGUI/port/sdl/Include/SDL2\
			$(makefile_dir)/ESDBox_IPGUI/port/sdl\
			$(makefile_dir)/ESDBox_IPGUI/core/misc\
			$(makefile_dir)/ESDBox_IPGUI/core/gfx\
			$(makefile_dir)/ESDBox_IPGUI/core/composite\
			$(makefile_dir)/ESDBox_IPGUI/core/composite/blend_color\
			$(makefile_dir)/ESDBox_IPGUI/core/composite/blend_gradient\
			$(makefile_dir)/ESDBox_IPGUI/core/composite/blend_image\
			$(makefile_dir)/ESDBox_IPGUI/core/image\
			$(makefile_dir)/ESDBox_IPGUI/core/image/decoder\
			$(makefile_dir)/ESDBox_IPGUI/core/image/proc\
			$(makefile_dir)/ESDBox_IPGUI/core/image/show\
			$(makefile_dir)/ESDBox_IPGUI/core/vector_render\
			$(makefile_dir)/ESDBox_IPGUI/core/ui/widget_manager\
			$(makefile_dir)/ESDBox_IPGUI/core/ui\
			$(makefile_dir)/ESDBox_IPGUI/core/ui/animation\
			$(makefile_dir)/ESDBox_IPGUI/core/ui/animation/builtin_anim\
			$(makefile_dir)/ESDBox_IPGUI/core/ui/layout\
			$(makefile_dir)/ESDBox_IPGUI/font/opensans\
			$(makefile_dir)/ESDBox_IPGUI/font/quicksand\
			$(makefile_dir)\
			$(makefile_dir)/ESDBox_IPGUI/charset
			
link_library_dir := $(makefile_dir)/elNET/port/Lib/x64
link_library := wpcap\
				Packet

link_library_dir += $(makefile_dir)/ESDBox_IPGUI/port/sdl/lib
link_library += SDL2main\
				SDL2
				
#静态库，链接时加-v可以看到链接库的名称
link_path ?= $(addprefix -L,$(link_library_dir))#静态库路径
link_flags := $(link_path) $(addprefix -l,$(link_library)) #静态库名称

ifneq ($(build_for_debug),0)
c_flags += -g -O0
else
c_flags += -O2
endif

c_srcfiles := $(foreach dir, $(csrc_dir), $(wildcard $(dir)/*.c))
#search C files in these dirs
vpath %.c $(sort $(dir $(c_srcfiles)))

asm_srcfiles ?= $(foreach dir, $(asm_dir), $(wildcard $(dir)/*.s))
asm_srcfiles += $(foreach dir, $(asm_dir), $(wildcard $(dir)/*.S))
#search ASM files in these dirs
vpath %.s $(sort $(dir $(asm_srcfiles)))

c_flags += $(ouptput_encode) $(input_encode)

# add c file header path
c_flags += $(addprefix -I,$(include_dir))

s_flags := $(c_flags) -x assembler-with-cpp

all : $(build_dir) $(build_dir)/$(target) debug_info

#非交叉编译一般不需要链接脚本
link_script := 

$(build_dir)/$(target) : $(obj_files)
	$(CC) -v $(link_script) $^ -o $@ $(link_flags)
#windows上编译x86程序使用ld链接就会产生错误，缺少C库不知道怎么解决，要手动添加一堆屌东西妈的，只能用gcc链接
#$(LD) $(link_script) $^ -o $@ $(link_flags)
	size $(build_dir)/$(target)$(executable_suffix)

$(build_dir)/%.o : %.c
	$(CC) -Wall $(c_flags) -c $< -o $@

$(build_dir)/%.o : %.s
	$(CC) -Wall $(s_flags) -c $< -o $@

$(build_dir) :
	mkdir $@

$(lib_dir) :
	mkdir $@

debug_info:
	@echo $(BUILD_INFO)

.PHONY: clean echo

echo:
	@echo $(obj_files)
	@echo $(build_dir)
	@echo $(c_srcfiles)
	@echo $(include_dir)
	@echo $(c_flags)
	@echo $(s_flags)
	@echo $(link_flags)

clean:
	-rm -fr $(build_dir)/*.o $(build_dir)/*.coff $(build_dir)/*.exe
