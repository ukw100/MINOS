# Makefile for MINOS
# System: Ubuntu 20.04, arm-none-eabi-gcc, gnu make
# st-flash from  https://github.com/stlink-org/stlink

CC := arm-none-eabi-gcc
LD := arm-none-eabi-gcc
OC := arm-none-eabi-objcopy

myname := minos

MODULES   := base board-led button cmd console delay fatfs fe font fs i2c i2c-at24c32 i2c-ds3231
MODULES	  += i2c-lcd ili9341 io mcurses nic sdcard ssd1963 tft stm32f4-rtc timer2 uart uart2 w25qxx ws2812

OPT := -Os

CFLAGS	:= -mthumb -mcpu=cortex-m4 -mfloat-abi=hard -mfpu=fpv4-sp-d16 $(OPT) --specs=nosys.specs -ffunction-sections -fdata-sections -Wall -Wextra

SRC_DIR   := src SPL/src $(addprefix src/,$(MODULES))
BUILD_DIR := build build/spl $(addprefix build/,$(MODULES))

SRCS      := $(foreach sdir,$(SRC_DIR),$(wildcard $(sdir)/*.c)) 
OBJS      := build/startup_stm32f4xx.o      
OBJS      += $(patsubst src/%.c,build/%.o,$(SRCS))
# Replace in OBJS "SPL/src/*.c" with "build/spl/*.o" 
OBJS	  := $(patsubst SPL/src/%.c,build/spl/%.o,$(OBJS)) 
INCLUDES  := $(addprefix -I,$(SRC_DIR)) -Iinc -Icmsis -ISPL/inc
DEFINES   := -DARM_MATH_CM4 -D__FPU_USED -DSTM32F407VE -DSTM32F407 -DSTM32F4XX -DUSE_STDPERIPH_DRIVER -DHSE_VALUE=8000000 -fno-strict-aliasing

LIBS := -lm

vpath %.c $(SRC_DIR) 

define make-goal
$1/%.o: %.c
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) -c $$< -o $$@
endef

.PHONY: all checkdirs clean

all: checkdirs build/$(myname).elf build/$(myname).bin build/$(myname).hex

build/startup_stm32f4xx.o: src/startup_stm32f4xx.S
	$(CC) $(CFLAGS) $(INCLUDES) $(DEFINES) -c $< -o $@
	
build/$(myname).elf: $(OBJS) 
	$(CC) $(CFLAGS) -T ./stm32f407ve_flash.ld -Xlinker --gc-sections -Wl,-Map,$(myname).map -o build/$(myname).elf $(OBJS) $(LIBS)

build/$(myname).hex: build/$(myname).elf
	$(OC) -O ihex build/$(myname).elf  build/$(myname).hex

build/$(myname).bin: build/$(myname).elf
	$(OC) -O binary build/$(myname).elf  build/$(myname).bin

checkdirs: $(BUILD_DIR)

$(BUILD_DIR):
	@mkdir -p $@

clean:
	@rm -rf $(BUILD_DIR)
	
flash:  build/$(myname).bin
	st-flash --format ihex --reset write ./build/$(myname).hex
	
comm:
	screen /dev/ttyUSB0 115200	

$(foreach bdir,$(BUILD_DIR),$(eval $(call make-goal,$(bdir))))
