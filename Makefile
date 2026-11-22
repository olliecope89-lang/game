#---------------------------------------------------------------------------------
# Floppy Fighters - PSL1GHT build
#
#   export PS3DEV=/usr/local/ps3dev
#   export PSL1GHT=$(PS3DEV)
#   make          # builds floppy-fighters.elf / .self
#   make pkg      # builds the installable floppy-fighters.pkg
#
# (Modelled on the Makefiles of the PSL1GHT samples. If your PSL1GHT version
#  names a variable differently, diff against any sample's Makefile.)
#---------------------------------------------------------------------------------
.SUFFIXES:
#---------------------------------------------------------------------------------
ifeq ($(strip $(PSL1GHT)),)
$(error "Please set PSL1GHT in your environment. export PSL1GHT=<path>")
endif

include $(PSL1GHT)/ppu_rules

#---------------------------------------------------------------------------------
TARGET		:=	floppy-fighters
BUILD		:=	build
SOURCES		:=	source
DATA		:=
INCLUDES	:=	source

TITLE		:=	Floppy Fighters
APPID		:=	FLOP00001
CONTENTID	:=	UP0001-$(APPID)_00-0000000000000000
ICON0		:=	$(CURDIR)/ICON0.PNG
SFOXML		:=	$(CURDIR)/sfo.xml

#---------------------------------------------------------------------------------
CFLAGS		=	-O2 -Wall -mcpu=cell $(MACHDEP) $(INCLUDE)
CXXFLAGS	=	$(CFLAGS)
LDFLAGS		=	$(MACHDEP) -Wl,-Map,$(notdir $@).map

LIBS		:=	-lrsx -lgcm_sys -lio -lsysutil -lrt -llv2 -lm
LIBDIRS		:=	$(PORTLIBS)

#---------------------------------------------------------------------------------
ifneq ($(BUILD),$(notdir $(CURDIR)))
#---------------------------------------------------------------------------------

export OUTPUT	:=	$(CURDIR)/$(TARGET)
export VPATH	:=	$(foreach dir,$(SOURCES),$(CURDIR)/$(dir))
export BUILDDIR	:=	$(CURDIR)/$(BUILD)
export DEPSDIR	:=	$(BUILDDIR)

CFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.c)))
CXXFILES	:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.cpp)))
SFILES		:=	$(foreach dir,$(SOURCES),$(notdir $(wildcard $(dir)/*.S)))

export LD	:=	$(CC)

export OFILES	:=	$(CFILES:.c=.o) $(CXXFILES:.cpp=.o) $(SFILES:.S=.o)

export INCLUDE	:=	$(foreach dir,$(INCLUDES),-I$(CURDIR)/$(dir)) \
					$(foreach dir,$(LIBDIRS),-I$(dir)/include) \
					$(LIBPSL1GHT_INC) \
					-I$(CURDIR)/$(BUILD)

export LIBPATHS	:=	$(foreach dir,$(LIBDIRS),-L$(dir)/lib) \
					$(LIBPSL1GHT_LIB)

.PHONY: $(BUILD) clean pkg run

$(BUILD):
	@[ -d $@ ] || mkdir -p $@
	@$(MAKE) --no-print-directory -C $(BUILD) -f $(CURDIR)/Makefile

clean:
	@echo clean ...
	@rm -fr $(BUILD) $(OUTPUT).elf $(OUTPUT).self $(OUTPUT).a *.pkg

run:
	ps3load $(OUTPUT).self

pkg: $(BUILD) $(OUTPUT).pkg

#---------------------------------------------------------------------------------
else
#---------------------------------------------------------------------------------

DEPENDS	:=	$(OFILES:.o=.d)

$(OUTPUT).self: $(OUTPUT).elf
$(OUTPUT).elf:	$(OFILES)

-include $(DEPENDS)

#---------------------------------------------------------------------------------
endif
#---------------------------------------------------------------------------------
