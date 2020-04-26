#include <Uefi.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/PciIo.h>
#include <Protocol/DriverSupportedEfiVersion.h>
#include <Protocol/DevicePath.h>

#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#include <Library/UefiLib.h>
#include <Library/PcdLib.h>
#include <Library/DebugLib.h>
#include <Library/DevicePathLib.h>
#include <Library/FrameBufferBltLib.h>
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiDriverEntryPoint.h>

#include <IndustryStandard/Pci.h>
#include <IndustryStandard/Acpi.h>

//registers are in bar 0
//frame buffer is in bar 2
#define PCH_DISPLAY_BASE	0xc0000u

#define gmbusSelect (PCH_DISPLAY_BASE+0x5100)
#define   GMBUS_AKSV_SELECT	(1 << 11)
#define   GMBUS_RATE_100KHZ	(0 << 8)
#define   GMBUS_RATE_50KHZ	(1 << 8)
#define   GMBUS_RATE_400KHZ	(2 << 8) /* reserved on Pineview */
#define   GMBUS_RATE_1MHZ	(3 << 8) /* reserved on Pineview */
#define   GMBUS_HOLD_EXT	(1 << 7) /* 300ns hold time, rsvd on Pineview */
#define   GMBUS_BYTE_CNT_OVERRIDE (1 << 6)
#define   GMBUS_PIN_DISABLED	0
#define   GMBUS_PIN_SSC		1
#define   GMBUS_PIN_VGADDC	2
#define   GMBUS_PIN_PANEL	3
#define   GMBUS_PIN_DPD_CHV	3 /* HDMID_CHV */
#define   GMBUS_PIN_DPC		4 /* HDMIC */
#define   GMBUS_PIN_DPB		5 /* SDVO, HDMIB */
#define   GMBUS_PIN_DPD		6 /* HDMID */
#define   GMBUS_PIN_RESERVED	7 /* 7 reserved */
#define   GMBUS_PIN_1_BXT	1 /* BXT+ (atom) and CNP+ (big core) */
#define   GMBUS_PIN_2_BXT	2
#define   GMBUS_PIN_3_BXT	3
#define   GMBUS_PIN_4_CNP	4
#define   GMBUS_PIN_9_TC1_ICP	9
#define   GMBUS_PIN_10_TC2_ICP	10
#define   GMBUS_PIN_11_TC3_ICP	11
#define   GMBUS_PIN_12_TC4_ICP	12

#define gmbusCommand (PCH_DISPLAY_BASE+0x5104)
#define   GMBUS_SW_CLR_INT	(1 << 31)
#define   GMBUS_SW_RDY		(1 << 30)
#define   GMBUS_ENT		(1 << 29) /* enable timeout */
#define   GMBUS_CYCLE_NONE	(0 << 25)
#define   GMBUS_CYCLE_WAIT	(1 << 25)
#define   GMBUS_CYCLE_INDEX	(2 << 25)
#define   GMBUS_CYCLE_STOP	(4 << 25)
#define   GMBUS_BYTE_COUNT_SHIFT 16
#define   GMBUS_BYTE_COUNT_MAX   256U
#define   GEN9_GMBUS_BYTE_COUNT_MAX 511U
#define   GMBUS_SLAVE_INDEX_SHIFT 8
#define   GMBUS_SLAVE_ADDR_SHIFT 1
#define   GMBUS_SLAVE_READ	(1 << 0)
#define   GMBUS_SLAVE_WRITE	(0 << 0)

#define gmbusStatus (PCH_DISPLAY_BASE+0x5108)
#define   GMBUS_INUSE		(1 << 15)
#define   GMBUS_HW_WAIT_PHASE	(1 << 14)
#define   GMBUS_STALL_TIMEOUT	(1 << 13)
#define   GMBUS_INT		(1 << 12)
#define   GMBUS_HW_RDY		(1 << 11)
#define   GMBUS_SATOER		(1 << 10)
#define   GMBUS_ACTIVE		(1 << 9)

#define gmbusData (PCH_DISPLAY_BASE+0x510C)

#define _PCH_DP_B		(0xe4100)
#define _PCH_DPB_AUX_CH_CTL	(0xe4110)
#define _PCH_DPB_AUX_CH_DATA1	(0xe4114)
#define _PCH_DPB_AUX_CH_DATA2	(0xe4118)
#define _PCH_DPB_AUX_CH_DATA3	(0xe411c)
#define _PCH_DPB_AUX_CH_DATA4	(0xe4120)
#define _PCH_DPB_AUX_CH_DATA5	(0xe4124)

#define _DPA_AUX_CH_CTL		(0x64010)
#define _DPA_AUX_CH_DATA1	(0x64014)
#define _DPA_AUX_CH_DATA2	(0x64018)
#define _DPA_AUX_CH_DATA3	(0x6401c)
#define _DPA_AUX_CH_DATA4	(0x64020)
#define _DPA_AUX_CH_DATA5	(0x64024)

#define   DP_AUX_CH_CTL_SEND_BUSY	    (1 << 31)
#define   DP_AUX_CH_CTL_DONE		    (1 << 30)
#define   DP_AUX_CH_CTL_INTERRUPT	    (1 << 29)
#define   DP_AUX_CH_CTL_TIME_OUT_ERROR	    (1 << 28)
#define   DP_AUX_CH_CTL_TIME_OUT_400us	    (0 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_600us	    (1 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_800us	    (2 << 26)
#define   DP_AUX_CH_CTL_TIME_OUT_MAX	    (3 << 26) /* Varies per platform */
#define   DP_AUX_CH_CTL_TIME_OUT_MASK	    (3 << 26)
#define   DP_AUX_CH_CTL_RECEIVE_ERROR	    (1 << 25)
#define   DP_AUX_CH_CTL_MESSAGE_SIZE_MASK    (0x1f << 20)
#define   DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT   20
#define   DP_AUX_CH_CTL_PRECHARGE_2US_MASK   (0xf << 16)
#define   DP_AUX_CH_CTL_PRECHARGE_2US_SHIFT  16
#define   DP_AUX_CH_CTL_AUX_AKSV_SELECT	    (1 << 15)
#define   DP_AUX_CH_CTL_MANCHESTER_TEST	    (1 << 14)
#define   DP_AUX_CH_CTL_SYNC_TEST	    (1 << 13)
#define   DP_AUX_CH_CTL_DEGLITCH_TEST	    (1 << 12)
#define   DP_AUX_CH_CTL_PRECHARGE_TEST	    (1 << 11)
#define   DP_AUX_CH_CTL_BIT_CLOCK_2X_MASK    (0x7ff)
#define   DP_AUX_CH_CTL_BIT_CLOCK_2X_SHIFT   0
#define   DP_AUX_CH_CTL_PSR_DATA_AUX_REG_SKL	(1 << 14)
#define   DP_AUX_CH_CTL_FS_DATA_AUX_REG_SKL	(1 << 13)
#define   DP_AUX_CH_CTL_GTC_DATA_AUX_REG_SKL	(1 << 12)
#define   DP_AUX_CH_CTL_TBT_IO			(1 << 11)
#define   DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL_MASK (0x1f << 5)
#define   DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(c) (((c) - 1) << 5)
#define   DP_AUX_CH_CTL_SYNC_PULSE_SKL(c)   ((c) - 1)

#define AUX_NATIVE_WRITE			0x8
#define AUX_NATIVE_READ			0x9
#define AUX_I2C_WRITE			0x0
#define AUX_I2C_READ			0x1
#define AUX_I2C_STATUS			0x2
#define AUX_I2C_MOT				0x4
#define AUX_I2C_REPLY_ACK			0x0

#define VGACNTRL		(0x71400)
#define VGA_DISP_DISABLE			(1 << 31)
#define VGA_2X_MODE				(1 << 30)

/* Pipe A timing regs */
#define HTOTAL_A	0x60000
#define HBLANK_A	0x60004
#define HSYNC_A	0x60008
#define VTOTAL_A	0x6000c
#define VBLANK_A	0x60010
#define VSYNC_A	0x60014
#define PIPEASRC	0x6001c
#define BCLRPAT_A	0x60020
#define VSYNCSHIFT_A	0x60028
#define PIPE_MULT_A	0x6002c

#define _PIPEACONF		0x70008
#define   PIPECONF_ENABLE	(1 << 31)
#define   PIPECONF_DISABLE	0
#define   I965_PIPECONF_ACTIVE	(1 << 30)

#define _DSPACNTR				0x70180
#define   DISPLAY_PLANE_ENABLE			(1 << 31)
#define   DISPLAY_PLANE_DISABLE			0
#define   PLANE_CTL_FORMAT_MASK			(0xf << 24)
#define   PLANE_CTL_FORMAT_YUV422		(0 << 24)
#define   PLANE_CTL_FORMAT_NV12			(1 << 24)
#define   PLANE_CTL_FORMAT_XRGB_2101010		(2 << 24)
#define   PLANE_CTL_FORMAT_XRGB_8888		(4 << 24)
#define   PLANE_CTL_ORDER_BGRX			(0 << 20)
#define   PLANE_CTL_ORDER_RGBX			(1 << 20)
#define   PLANE_CTL_ALPHA_MASK			(0x3 << 4) /* Pre-GLK */
#define   PLANE_CTL_ALPHA_DISABLE		(0 << 4)
#define   PLANE_CTL_ALPHA_SW_PREMULTIPLY	(2 << 4)
#define   PLANE_CTL_ALPHA_HW_PREMULTIPLY	(3 << 4)

#define   DISPPLANE_PIXFORMAT_MASK		(0xf << 26)
#define   DISPPLANE_YUV422			(0x0 << 26)
#define   DISPPLANE_8BPP			(0x2 << 26)
#define   DISPPLANE_BGRA555			(0x3 << 26)
#define   DISPPLANE_BGRX555			(0x4 << 26)
#define   DISPPLANE_BGRX565			(0x5 << 26)
#define   DISPPLANE_BGRX888			(0x6 << 26)
#define   DISPPLANE_BGRA888			(0x7 << 26)
#define   DISPPLANE_RGBX101010			(0x8 << 26)
#define   DISPPLANE_RGBA101010			(0x9 << 26)
#define   DISPPLANE_BGRX101010			(0xa << 26)
#define   DISPPLANE_RGBX161616			(0xc << 26)
#define   DISPPLANE_RGBX888			(0xe << 26)
#define   DISPPLANE_RGBA888			(0xf << 26)

#define _DSPAADDR				0x70184
#define _DSPASTRIDE				0x70188
#define _DSPAPOS				0x7018C /* reserved */
#define _DSPASIZE				0x70190
#define _DSPASURF				0x7019C /* 965+ only */
#define _DSPATILEOFF				0x701A4 /* 965+ only */
#define _DSPAOFFSET				0x701A4 /* HSW */
#define _DSPASURFLIVE				0x701AC

STATIC EFI_GRAPHICS_OUTPUT_MODE_INFORMATION g_mode_info[] = {
  {
    0,    // Version
    1024,  // HorizontalResolution
    768,  // VerticalResolution
  }
};

STATIC EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE g_mode = {
  ARRAY_SIZE (g_mode_info),                // MaxMode
  0,                                              // Mode
  g_mode_info,                             // Info
  sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION),  // SizeOfInfo
};

#pragma pack(1)
typedef struct {
	UINT8 magic[8];
	UINT16 vendorId;
	UINT16 productId;
	UINT32 serialNumber;
	UINT8 manufactureWeek;
	UINT8 manufactureYear;
	UINT8 structVersion;
	UINT8 structRevision;
	UINT8 inputParameters;
	UINT8 screenWidth;
	UINT8 screenHeight;
	UINT8 gamma;
	UINT8 features;
	UINT8 colorCoordinates[10];
	UINT8 estTimings1;
	UINT8 estTimings2;
	UINT8 vendorTimings;
	struct {
		UINT8 resolution;
		UINT8 frequency;
	} standardTimings[8];
	struct {
		UINT16 pixelClock;
		UINT8 horzActive;
		UINT8 horzBlank;
		UINT8 horzActiveBlankMsb;
		UINT8 vertActive;
		UINT8 vertBlank;
		UINT8 vertActiveBlankMsb;
		UINT8 horzSyncOffset;
		UINT8 horzSyncPulse;
		UINT8 vertSync;
		UINT8 syncMsb;
		UINT8 dimensionWidth;
		UINT8 dimensionHeight;
		UINT8 dimensionMsb;
		UINT8 horzBorder;
		UINT8 vertBorder;
		UINT8 features;
	} detailTimings[4];
	UINT8 numExtensions;
	UINT8 checksum;
} EDID;
#pragma pack()

typedef struct {
  UINT64                                Signature;
  EFI_HANDLE                            Handle;
  EFI_PCI_IO_PROTOCOL                   *PciIo;
  EFI_GRAPHICS_OUTPUT_PROTOCOL          GraphicsOutput;
  EFI_DEVICE_PATH_PROTOCOL              *GopDevicePath;
  EDID edid;
  EFI_PHYSICAL_ADDRESS FbBase;
  UINT32 stride;
  UINT32 gmadr;
} I915_VIDEO_PRIVATE_DATA;

I915_VIDEO_PRIVATE_DATA g_private={SIGNATURE_32('i','9','1','5')};

static void write32(UINT64 reg, UINT32 data){
	g_private.PciIo->Mem.Write (
		g_private.PciIo,
		EfiPciIoWidthFillUint32,
		PCI_BAR_IDX0,
		reg,
		1,
		&data
	);
}

static UINT32 read32(UINT64 reg){
	UINT32 data=0;
	g_private.PciIo->Mem.Read (
		g_private.PciIo,
		EfiPciIoWidthFillUint32,
		PCI_BAR_IDX0,
		reg,
		1,
		&data
	);
	return data;
}

static UINT64 read64(UINT64 reg){
	UINT64 data=0;
	g_private.PciIo->Mem.Read (
		g_private.PciIo,
		EfiPciIoWidthFillUint64,
		PCI_BAR_IDX0,
		reg,
		1,
		&data
	);
	return data;
}

static EFI_STATUS gmbusWait(UINT32 wanted){
	for(;;){
		UINT32 status=read32(gmbusStatus);
		if(status&GMBUS_SATOER){
			//failed
			return EFI_DEVICE_ERROR;
		}
		if(status&wanted){
			//worked
			return EFI_SUCCESS;
		}
	}
}

static EFI_STATUS ReadEDID(EDID* result){
	UINT32 pin=0;
	//it's an INTEL GPU, there's no way we could be big endian
	UINT32* p=(UINT32*)result;
	//try all the pins on GMBUS
	for(pin=2;pin<=6;pin++){
		DebugPrint(EFI_D_ERROR,"i915: trying pin %d\n",pin);
		write32(gmbusSelect, pin);
		if(EFI_ERROR(gmbusWait(GMBUS_HW_RDY))){
			//it's DP, need to hack AUX_CHAN
			continue;
		}
		//set read offset: i2cWrite(0x50, &offset, 1);
		write32(gmbusData, 0);
		write32(gmbusCommand, (0x50<<GMBUS_SLAVE_ADDR_SHIFT)|(1<<GMBUS_BYTE_COUNT_SHIFT)|GMBUS_SLAVE_WRITE|GMBUS_CYCLE_WAIT|GMBUS_SW_RDY);
		//gmbusWait(GMBUS_HW_WAIT_PHASE);
		gmbusWait(GMBUS_HW_RDY);
		//read the edid: i2cRead(0x50, &edid, 128);
		//note that we could fail here!
		write32(gmbusCommand, (0x50<<GMBUS_SLAVE_ADDR_SHIFT)|(128<<GMBUS_BYTE_COUNT_SHIFT)|GMBUS_SLAVE_READ|GMBUS_CYCLE_WAIT|GMBUS_SW_RDY);
		UINT32 i=0;
		for(i=0;i<128;i+=4){
			if(EFI_ERROR(gmbusWait(GMBUS_HW_RDY))){break;}
			p[i>>2]=read32(gmbusData);
		}
		//gmbusWait(GMBUS_HW_WAIT_PHASE);
		gmbusWait(GMBUS_HW_RDY);
		for(UINT32 i=0;i<16;i++){
			for(UINT32 j=0;j<8;j++){
				DebugPrint(EFI_D_ERROR,"%02x ",((UINT8*)(p))[i*8+j]);
			}
			DebugPrint(EFI_D_ERROR,"\n");
		}
		if(i>=128&&*(UINT64*)result->magic==0x00FFFFFFFFFFFF00uLL){return EFI_SUCCESS;}
	}
	//try DP AUX CHAN - Skylake
	//write32(_DPA_AUX_CH_CTL+(1<<8),0x1234)
	//write32(_DPA_AUX_CH_CTL+(0x600),0x1234);
	//write32(_DPA_AUX_CH_CTL+(0<<8),0x1234);
	//write32(_DPA_AUX_CH_DATA1+(0<<8),0xabcd);
	//write32(_DPA_AUX_CH_DATA2+(0<<8),0xabcd);
	//write32(_DPA_AUX_CH_DATA3+(0<<8),0xabcd);
	//DebugPrint(EFI_D_ERROR,"i915: SKL CTL %08x\n",read32(_DPA_AUX_CH_CTL+(0<<8)));
	//DebugPrint(EFI_D_ERROR,"i915: SKL DATA %08x\n",read32(_DPA_AUX_CH_DATA1+(0<<8)));
	//DebugPrint(EFI_D_ERROR,"i915: SKL DATA %08x\n",read32(_DPA_AUX_CH_DATA2+(0<<8)));
	//DebugPrint(EFI_D_ERROR,"i915: SKL DATA %08x\n",read32(_DPA_AUX_CH_DATA3+(0<<8)));
	//write32(_PCH_DP_B+(1<<8),0x1234);
	//DebugPrint(EFI_D_ERROR,"i915: SKL %08x\n",read32(_DPA_AUX_CH_CTL+(1<<8)));
	//DebugPrint(EFI_D_ERROR,"i915: PCH %08x\n",read32(_PCH_DP_B+(1<<8)));
	for(pin=0;pin<=5;pin++){
		DebugPrint(EFI_D_ERROR,"i915: trying DP aux %d\n",pin);
		//aux message header is 3-4 bytes: ctrl8 addr16 len8
		//the data is big endian
		//len is receive buffer size-1
		//i2c init
		UINT32 send_ctl=(
	      DP_AUX_CH_CTL_SEND_BUSY |
	      DP_AUX_CH_CTL_DONE |
	      DP_AUX_CH_CTL_TIME_OUT_ERROR |
	      DP_AUX_CH_CTL_TIME_OUT_MAX |
	      DP_AUX_CH_CTL_RECEIVE_ERROR |
	      (3 << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
	      DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(32) |
	      DP_AUX_CH_CTL_SYNC_PULSE_SKL(32)
		);
		/* Must try at least 3 times according to DP spec, WHICH WE DON'T CARE */
		write32(_DPA_AUX_CH_DATA1+(pin<<8), ((AUX_I2C_MOT|AUX_I2C_WRITE)<<28)|(0x50<<8)|0);
		write32(_DPA_AUX_CH_CTL+(pin<<8), send_ctl);
		UINT32 aux_status;
		UINT32 counter=0;
		for(;;){
			aux_status=read32(_DPA_AUX_CH_CTL+(pin<<8));
			if(!(aux_status&DP_AUX_CH_CTL_SEND_BUSY)){break;}
			counter+=1;
			if(counter>=16384){break;}
		}
		write32(_DPA_AUX_CH_CTL+(pin<<8), 
			aux_status |
			DP_AUX_CH_CTL_DONE |
			DP_AUX_CH_CTL_TIME_OUT_ERROR |
			DP_AUX_CH_CTL_RECEIVE_ERROR
		);
		//i2c send 1 byte
		send_ctl=(
		     DP_AUX_CH_CTL_SEND_BUSY |
		     DP_AUX_CH_CTL_DONE |
		     DP_AUX_CH_CTL_TIME_OUT_ERROR |
		     DP_AUX_CH_CTL_TIME_OUT_MAX |
		     DP_AUX_CH_CTL_RECEIVE_ERROR |
		     (5 << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
		     DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(32) |
		     DP_AUX_CH_CTL_SYNC_PULSE_SKL(32)
		);
		write32(_DPA_AUX_CH_DATA1+(pin<<8), (AUX_I2C_WRITE<<28)|(0x50<<8)|0);
		write32(_DPA_AUX_CH_DATA2+(pin<<8), 0);
		write32(_DPA_AUX_CH_CTL+(pin<<8), send_ctl);
		counter=0;
		for(;;){
			aux_status=read32(_DPA_AUX_CH_CTL+(pin<<8));
			if(!(aux_status&DP_AUX_CH_CTL_SEND_BUSY)){break;}
			counter+=1;
			if(counter>=16384){break;}
		}
		write32(_DPA_AUX_CH_CTL+(pin<<8), 
			aux_status |
			DP_AUX_CH_CTL_DONE |
			DP_AUX_CH_CTL_TIME_OUT_ERROR |
			DP_AUX_CH_CTL_RECEIVE_ERROR
		);
		if (aux_status & (DP_AUX_CH_CTL_TIME_OUT_ERROR|DP_AUX_CH_CTL_RECEIVE_ERROR)){
			continue;
		}
		//i2c read 1 byte * 128
		DebugPrint(EFI_D_ERROR,"i915: reading DP aux %d\n",pin);
		//aux message header is 3-4 bytes: ctrl8 addr16 len8
		//the data is big endian
		//len is receive buffer size-1
		//i2c init
		send_ctl=(
		     DP_AUX_CH_CTL_SEND_BUSY |
		     DP_AUX_CH_CTL_DONE |
		     DP_AUX_CH_CTL_TIME_OUT_ERROR |
		     DP_AUX_CH_CTL_TIME_OUT_MAX |
		     DP_AUX_CH_CTL_RECEIVE_ERROR |
		     (3 << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
		     DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(32) |
		     DP_AUX_CH_CTL_SYNC_PULSE_SKL(32)
		);
		/* Must try at least 3 times according to DP spec, WHICH WE DON'T CARE */
		write32(_DPA_AUX_CH_DATA1+(pin<<8), ((AUX_I2C_MOT|AUX_I2C_READ)<<28)|(0x50<<8)|0);
		write32(_DPA_AUX_CH_CTL+(pin<<8), send_ctl);
		counter=0;
		for(;;){
			aux_status=read32(_DPA_AUX_CH_CTL+(pin<<8));
			if(!(aux_status&DP_AUX_CH_CTL_SEND_BUSY)){break;}
			counter+=1;
			if(counter>=16384){break;}
		}
		write32(_DPA_AUX_CH_CTL+(pin<<8), 
			aux_status |
			DP_AUX_CH_CTL_DONE |
			DP_AUX_CH_CTL_TIME_OUT_ERROR |
			DP_AUX_CH_CTL_RECEIVE_ERROR
		);
		UINT32 i=0;
		for(i=0;i<128;i++){
			send_ctl=(
			     DP_AUX_CH_CTL_SEND_BUSY |
			     DP_AUX_CH_CTL_DONE |
			     DP_AUX_CH_CTL_TIME_OUT_ERROR |
			     DP_AUX_CH_CTL_TIME_OUT_MAX |
			     DP_AUX_CH_CTL_RECEIVE_ERROR |
			     (4 << DP_AUX_CH_CTL_MESSAGE_SIZE_SHIFT) |
			     DP_AUX_CH_CTL_FW_SYNC_PULSE_SKL(32) |
			     DP_AUX_CH_CTL_SYNC_PULSE_SKL(32)
			);
			write32(_DPA_AUX_CH_DATA1+(pin<<8), (AUX_I2C_READ<<28)|(0x50<<8)|0);
			write32(_DPA_AUX_CH_CTL+(pin<<8), send_ctl);
			counter=0;
			for(;;){
				aux_status=read32(_DPA_AUX_CH_CTL+(pin<<8));
				if(!(aux_status&DP_AUX_CH_CTL_SEND_BUSY)){break;}
				counter+=1;
				if(counter>=16384){break;}
			}
			write32(_DPA_AUX_CH_CTL+(pin<<8), 
				aux_status |
				DP_AUX_CH_CTL_DONE |
				DP_AUX_CH_CTL_TIME_OUT_ERROR |
				DP_AUX_CH_CTL_RECEIVE_ERROR
			);
			UINT32 word=read32(_DPA_AUX_CH_DATA1+(pin<<8));
			((UINT8*)p)[i]=(word>>16)&0xff;
		}
		if(i>=128&&*(UINT64*)result->magic==0x00FFFFFFFFFFFF00uLL){return EFI_SUCCESS;}
	}
	return EFI_NOT_FOUND;
}

STATIC EFI_STATUS EFIAPI i915GraphicsOutputQueryMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  UINT32                                ModeNumber,
  OUT UINTN                                 *SizeOfInfo,
  OUT EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  **Info
  )
{
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION  *ModeInfo;
  DebugPrint(EFI_D_ERROR,"i915: query mode\n");

  if (Info == NULL || SizeOfInfo == NULL ||
      ModeNumber >= g_mode.MaxMode) {
    return EFI_INVALID_PARAMETER;
  }
  ModeInfo = &g_mode_info[ModeNumber];

  *Info = AllocateCopyPool (sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION), ModeInfo);
  if (*Info == NULL) {
    return EFI_OUT_OF_RESOURCES;
  }
  *SizeOfInfo = sizeof (EFI_GRAPHICS_OUTPUT_MODE_INFORMATION);

  return EFI_SUCCESS;
}

STATIC FRAME_BUFFER_CONFIGURE        *g_i915FrameBufferBltConfigure;
STATIC UINTN                         g_i915FrameBufferBltConfigureSize;
STATIC EFI_STATUS EFIAPI i915GraphicsOutputSetMode (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  IN  UINT32                       ModeNumber
  )
{
	DebugPrint(EFI_D_ERROR,"i915: set mode %u\n",ModeNumber);
	//TODO: DPLL
	
	//program PIPE_A
	UINT32 horz_active = g_private.edid.detailTimings[0].horzActive
			| ((UINT32)(g_private.edid.detailTimings[0].horzActiveBlankMsb >> 4) << 8);
	UINT32 horz_blank = g_private.edid.detailTimings[0].horzBlank
			| ((UINT32)(g_private.edid.detailTimings[0].horzActiveBlankMsb & 0xF) << 8);
	UINT32 horz_sync_offset = g_private.edid.detailTimings[0].horzSyncOffset
			| ((UINT32)(g_private.edid.detailTimings[0].syncMsb >> 6) << 8);
	UINT32 horz_sync_pulse = g_private.edid.detailTimings[0].horzSyncPulse
			| (((UINT32)(g_private.edid.detailTimings[0].syncMsb >> 4) & 0x3) << 8);
	
	UINT32 horizontal_active = horz_active;
	UINT32 horizontal_syncStart = horz_active + horz_sync_offset;
	UINT32 horizontal_syncEnd = horz_active + horz_sync_offset + horz_sync_pulse;
	UINT32 horizontal_total = horz_active + horz_blank;
	
	UINT32 vert_active =  g_private.edid.detailTimings[0].vertActive
			| ((UINT32)(g_private.edid.detailTimings[0].vertActiveBlankMsb >> 4) << 8);
	UINT32 vert_blank = g_private.edid.detailTimings[0].vertBlank
			| ((UINT32)(g_private.edid.detailTimings[0].vertActiveBlankMsb & 0xF) << 8);
	UINT32 vert_sync_offset = (g_private.edid.detailTimings[0].vertSync >> 4)
			| (((UINT32)(g_private.edid.detailTimings[0].syncMsb >> 2) & 0x3) << 4);
	UINT32 vert_sync_pulse = (g_private.edid.detailTimings[0].vertSync & 0xF)
			| ((UINT32)(g_private.edid.detailTimings[0].syncMsb & 0x3) << 4);
	
	UINT32 vertical_active = vert_active;
	UINT32 vertical_syncStart = vert_active + vert_sync_offset;
	UINT32 vertical_syncEnd = vert_active + vert_sync_offset + vert_sync_pulse;
	UINT32 vertical_total = vert_active + vert_blank;
	
	write32(VSYNCSHIFT_A, 0);
	
	write32(HTOTAL_A,
		   (horizontal_active - 1) |
		   ((horizontal_total - 1) << 16));
	write32(HBLANK_A,
		   (horizontal_active - 1) |
		   ((horizontal_total - 1) << 16));
	write32(HSYNC_A,
		   (horizontal_syncStart - 1) |
		   ((horizontal_syncEnd - 1) << 16));
	
	write32(VTOTAL_A,
		   (vertical_active - 1) |
		   ((vertical_total - 1) << 16));
	write32(VBLANK_A,
		   (vertical_active - 1) |
		   ((vertical_total - 1) << 16));
	write32(VSYNC_A,
		   (vertical_syncStart - 1) |
		   ((vertical_syncEnd - 1) << 16));
	UINT32 word=read32(_PIPEACONF);
	write32(_PIPEACONF,word|PIPECONF_ENABLE);
	for(;;){
		if(read32(_PIPEACONF)&I965_PIPECONF_ACTIVE){
			break;
		}
	}
	DebugPrint(EFI_D_ERROR,"i915: pipe enabled\n");
	
	//plane
	UINT32 stride=(horizontal_active*4+63)&-64;
	g_private.stride=stride;
	write32(_DSPAOFFSET,0);
	write32(_DSPASTRIDE,stride>>6);
	write32(_DSPASIZE,(horizontal_active - 1) | ((vertical_active-1)<<16));
	write32(_DSPAADDR,0);
	write32(_DSPASURF,g_private.gmadr);
	//word=read32(_DSPACNTR);
	//write32(_DSPACNTR,(word&~PLANE_CTL_FORMAT_MASK)|DISPLAY_PLANE_ENABLE|PLANE_CTL_FORMAT_XRGB_8888);
	write32(_DSPACNTR,DISPLAY_PLANE_ENABLE|PLANE_CTL_FORMAT_XRGB_8888);
	//write32(_DSPACNTR,DISPLAY_PLANE_ENABLE|DISPPLANE_BGRX888);
	DebugPrint(EFI_D_ERROR,"i915: plane enabled, dspcntr: %08x\n",read32(_DSPACNTR));
	g_mode.FrameBufferBase=g_private.FbBase;
	g_mode.FrameBufferSize=stride*vertical_active;
	
	//blt stuff
	EFI_STATUS Status;
	Status = FrameBufferBltConfigure (
	           (VOID*)(UINTN)g_mode.FrameBufferBase,
	           g_mode_info,
	           g_i915FrameBufferBltConfigure,
	           &g_i915FrameBufferBltConfigureSize
	           );

	if (Status == RETURN_BUFFER_TOO_SMALL) {
	  if (g_i915FrameBufferBltConfigure != NULL) {
	    FreePool (g_i915FrameBufferBltConfigure);
	  }
	  g_i915FrameBufferBltConfigure =
	    AllocatePool (g_i915FrameBufferBltConfigureSize);
	  if (g_i915FrameBufferBltConfigure == NULL) {
	    g_i915FrameBufferBltConfigureSize = 0;
	    return EFI_OUT_OF_RESOURCES;
	  }

	  Status = FrameBufferBltConfigure (
	             (VOID*)(UINTN)g_mode.FrameBufferBase,
	             g_mode_info,
	             g_i915FrameBufferBltConfigure,
	             &g_i915FrameBufferBltConfigureSize
	             );
	}
	
	return EFI_SUCCESS;
}

STATIC EFI_STATUS EFIAPI i915GraphicsOutputBlt (
  IN  EFI_GRAPHICS_OUTPUT_PROTOCOL          *This,
  IN  EFI_GRAPHICS_OUTPUT_BLT_PIXEL         *BltBuffer, OPTIONAL
  IN  EFI_GRAPHICS_OUTPUT_BLT_OPERATION     BltOperation,
  IN  UINTN                                 SourceX,
  IN  UINTN                                 SourceY,
  IN  UINTN                                 DestinationX,
  IN  UINTN                                 DestinationY,
  IN  UINTN                                 Width,
  IN  UINTN                                 Height,
  IN  UINTN                                 Delta
  )
{
	DebugPrint(EFI_D_ERROR,"i915: blt\n");
	return FrameBufferBlt (
	         g_i915FrameBufferBltConfigure,
	         BltBuffer,
	         BltOperation,
	         SourceX,
	         SourceY,
	         DestinationX,
	         DestinationY,
	         Width,
	         Height,
	         Delta
	         );
}

EFI_STATUS EFIAPI i915ControllerDriverStart (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
{
	EFI_TPL                           OldTpl;
	EFI_STATUS                        Status;
	I915_VIDEO_PRIVATE_DATA           *Private;
	PCI_TYPE00          Pci;
	
	OldTpl = gBS->RaiseTPL (TPL_CALLBACK);
	DebugPrint(EFI_D_ERROR,"i915: start\n");
	
	Private = &g_private;
	
	Private->Signature  = SIGNATURE_32('i','9','1','5');
	
	Status = gBS->OpenProtocol (
	                Controller,
	                &gEfiPciIoProtocolGuid,
	                (VOID **) &Private->PciIo,
	                This->DriverBindingHandle,
	                Controller,
	                EFI_OPEN_PROTOCOL_BY_DRIVER
	                );
	if (EFI_ERROR (Status)) {
	  goto RestoreTpl;
	}
	
	Status = Private->PciIo->Pci.Read (
	                      Private->PciIo,
	                      EfiPciIoWidthUint32,
	                      0,
	                      sizeof (Pci) / sizeof (UINT32),
	                      &Pci
	                      );
	if (EFI_ERROR (Status)) {
	  goto ClosePciIo;
	}
	
	Status = Private->PciIo->Attributes (
	                          Private->PciIo,
	                          EfiPciIoAttributeOperationEnable,
	                          EFI_PCI_DEVICE_ENABLE,// | EFI_PCI_IO_ATTRIBUTE_VGA_MEMORY,
	                          NULL
	                          );
	if (EFI_ERROR (Status)) {
	  goto ClosePciIo;
	}
	
	DebugPrint(EFI_D_ERROR,"i915: set pci attrs\n");
	
	//
	// Get ParentDevicePath
	//
	EFI_DEVICE_PATH_PROTOCOL          *ParentDevicePath;
	Status = gBS->HandleProtocol (
	                Controller,
	                &gEfiDevicePathProtocolGuid,
	                (VOID **) &ParentDevicePath
	                );
	if (EFI_ERROR (Status)) {
	  goto ClosePciIo;
	}

	//
	// Set Gop Device Path
	//
	ACPI_ADR_DEVICE_PATH              AcpiDeviceNode;
	ZeroMem (&AcpiDeviceNode, sizeof (ACPI_ADR_DEVICE_PATH));
	AcpiDeviceNode.Header.Type = ACPI_DEVICE_PATH;
	AcpiDeviceNode.Header.SubType = ACPI_ADR_DP;
	AcpiDeviceNode.ADR = ACPI_DISPLAY_ADR (1, 0, 0, 1, 0, ACPI_ADR_DISPLAY_TYPE_VGA, 0, 0);
	SetDevicePathNodeLength (&AcpiDeviceNode.Header, sizeof (ACPI_ADR_DEVICE_PATH));

	Private->GopDevicePath = AppendDevicePathNode (
	                                    ParentDevicePath,
	                                    (EFI_DEVICE_PATH_PROTOCOL *) &AcpiDeviceNode
	                                    );
	if (Private->GopDevicePath == NULL) {
	  Status = EFI_OUT_OF_RESOURCES;
	  goto ClosePciIo;
	}
	DebugPrint(EFI_D_ERROR,"i915: made gop path\n");
	
	//
	// Create new child handle and install the device path protocol on it.
	//
	Status = gBS->InstallMultipleProtocolInterfaces (
	                &Private->Handle,
	                &gEfiDevicePathProtocolGuid,
	                Private->GopDevicePath,
	                NULL
	                );
	if (EFI_ERROR (Status)) {
	  goto FreeGopDevicePath;
	}
	DebugPrint(EFI_D_ERROR,"i915: installed child handle\n");
	
	// query EDID and initialize the mode
	Status = ReadEDID(&g_private.edid);
	if (EFI_ERROR (Status)) {
		DebugPrint(EFI_D_ERROR,"i915: failed to read EDID\n");
		goto FreeGopDevicePath;
	}
	DebugPrint(EFI_D_ERROR,"i915: got EDID:\n");
	for(UINT32 i=0;i<16;i++){
		for(UINT32 j=0;j<8;j++){
			DebugPrint(EFI_D_ERROR,"%02x ",((UINT8*)(&g_private.edid))[i*8+j]);
		}
		DebugPrint(EFI_D_ERROR,"\n");
	}
	UINT32 pixel_clock = (UINT32)(g_private.edid.detailTimings[0].pixelClock) * 10;
	UINT32 x_active = g_private.edid.detailTimings[0].horzActive | ((UINT32)(g_private.edid.detailTimings[0].horzActiveBlankMsb >> 4) << 8);
	UINT32 y_active =  g_private.edid.detailTimings[0].vertActive | ((UINT32)(g_private.edid.detailTimings[0].vertActiveBlankMsb >> 4) << 8);
	DebugPrint(EFI_D_ERROR,"i915: %ux%u clock=%u\n",x_active,y_active,pixel_clock);
	g_mode_info[0].HorizontalResolution=x_active;
	g_mode_info[0].VerticalResolution=y_active;
	g_mode_info[0].PixelsPerScanLine = g_mode_info[0].HorizontalResolution;
	g_mode_info[0].PixelFormat = PixelBlueGreenRedReserved8BitPerColor;
	//disable VGA
	UINT32 vgaword=read32(VGACNTRL);
	write32(VGACNTRL,(vgaword&~VGA_2X_MODE)|VGA_DISP_DISABLE);
	DebugPrint(EFI_D_ERROR,"i915: bars %08x %08x %08x %08x\n",Pci.Device.Bar[0],Pci.Device.Bar[1],Pci.Device.Bar[2],Pci.Device.Bar[3]);
	//allocate BAR 2
	UINT32 bar_work=0xffffffff;
	Private->PciIo->Pci.Write (Private->PciIo,EfiPciIoWidthUint32,0x18,1,&bar_work);
	Private->PciIo->Pci.Read (Private->PciIo,EfiPciIoWidthUint32,0x18,1,&bar_work);
	UINTN MaxFbSize=~(bar_work&~0xf)+1;
	UINTN Pages = EFI_SIZE_TO_PAGES (MaxFbSize);
	EFI_PHYSICAL_ADDRESS FbBase = 0x80000000; 
	/*
	Status=gBS->AllocatePages (AllocateAddress,EfiReservedMemoryType,Pages,&FbBase);
	if( EFI_ERROR(Status) ){
		DebugPrint(EFI_D_ERROR,"i915: failed to allocate aperture\n");
		goto FreeGopDevicePath;
	}
	*/
	if (!FbBase) {
	  DebugPrint(EFI_D_ERROR,"i915: failed to allocate aperture\n");
	  Status=EFI_OUT_OF_RESOURCES;
	  goto FreeGopDevicePath;
	}
	DebugPrint(EFI_D_ERROR,"i915: aperture at %p, size %08x, %d pages\n",FbBase,MaxFbSize,Pages);
	Private->PciIo->Pci.Write (Private->PciIo,EfiPciIoWidthUint32,0x18,1,&FbBase);
	Private->PciIo->Pci.Read (Private->PciIo,EfiPciIoWidthUint32,0x18,1,&bar_work);
	DebugPrint(EFI_D_ERROR,"i915: aperture confirmed at %08x\n",bar_work);
	//GVT-g gmadr issue
	g_private.gmadr=0;
	if(read64(0x78000)==0x4776544776544776ULL){
		g_private.gmadr=read32(0x78040);
	}
	DebugPrint(EFI_D_ERROR,"i915: gmadr = %08x\n",g_private.gmadr);
	g_private.FbBase=FbBase;
	//test 
	for(INTN i=0;i<(32<<20);i+=2048){
		((UINT32*)FbBase)[i]=0x00ff0000|(i>>12);
	}
	//TODO: setup OpRegion from fw_cfg, turn on backlight
	
	//
	// Start the GOP software stack.
	//
	EFI_GRAPHICS_OUTPUT_PROTOCOL *GraphicsOutput;
	GraphicsOutput            = &Private->GraphicsOutput;
	GraphicsOutput->QueryMode = i915GraphicsOutputQueryMode;
	GraphicsOutput->SetMode   = i915GraphicsOutputSetMode;
	GraphicsOutput->Blt       = i915GraphicsOutputBlt;
	GraphicsOutput->Mode = &g_mode;
	Status = GraphicsOutput->SetMode (GraphicsOutput, 0);
	if (EFI_ERROR (Status)) {
		goto FreeGopDevicePath;
	}
	
	
	Status = gBS->InstallMultipleProtocolInterfaces (
	                &Private->Handle,
	                &gEfiGraphicsOutputProtocolGuid,
	                &Private->GraphicsOutput,
	                NULL
	                );
	if (EFI_ERROR (Status)) {
	  goto Destructi915Graphics;
	}
	
	//
	// Reference parent handle from child handle.
	//
	EFI_PCI_IO_PROTOCOL               *ChildPciIo;
	Status = gBS->OpenProtocol (
	              Controller,
	              &gEfiPciIoProtocolGuid,
	              (VOID **) &ChildPciIo,
	              This->DriverBindingHandle,
	              Private->Handle,
	              EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER
	              );
	if (EFI_ERROR (Status)) {
	  goto UninstallGop;
	}
	
	DebugPrint(EFI_D_ERROR,"i915: gop ready\n");
	
	gBS->RestoreTPL (OldTpl);
	return EFI_SUCCESS;

UninstallGop:
	gBS->UninstallProtocolInterface (Private->Handle,
           &gEfiGraphicsOutputProtocolGuid, &Private->GraphicsOutput);

Destructi915Graphics:

ClosePciIo:
	gBS->CloseProtocol (Controller, &gEfiPciIoProtocolGuid,
           This->DriverBindingHandle, Controller);

FreeGopDevicePath:
	FreePool (Private->GopDevicePath);
	
RestoreTpl:
	gBS->RestoreTPL (OldTpl);
	return Status;
}

EFI_STATUS EFIAPI i915ControllerDriverStop (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN UINTN                          NumberOfChildren,
  IN EFI_HANDLE                     *ChildHandleBuffer
  )
{
	DebugPrint(EFI_D_ERROR,"i915ControllerDriverStop\n");
	//TODO
	return EFI_UNSUPPORTED;
}


EFI_STATUS EFIAPI i915ControllerDriverSupported (
  IN EFI_DRIVER_BINDING_PROTOCOL    *This,
  IN EFI_HANDLE                     Controller,
  IN EFI_DEVICE_PATH_PROTOCOL       *RemainingDevicePath
  )
{
	EFI_STATUS          Status;
	EFI_PCI_IO_PROTOCOL *PciIo;
	PCI_TYPE00          Pci;
	EFI_DEV_PATH        *Node;

	//
	// Open the PCI I/O Protocol
	//
	Status = gBS->OpenProtocol (
	                Controller,
	                &gEfiPciIoProtocolGuid,
	                (VOID **) &PciIo,
	                This->DriverBindingHandle,
	                Controller,
	                EFI_OPEN_PROTOCOL_BY_DRIVER
	                );
	if (EFI_ERROR (Status)) {
	  return Status;
	}

	//
	// Read the PCI Configuration Header from the PCI Device
	//
	Status = PciIo->Pci.Read (
	                      PciIo,
	                      EfiPciIoWidthUint32,
	                      0,
	                      sizeof (Pci) / sizeof (UINT32),
	                      &Pci
	                      );
	if (EFI_ERROR (Status)) {
	  goto Done;
	}

	Status = EFI_UNSUPPORTED;
	if (Pci.Hdr.VendorId == 0x8086&&IS_PCI_DISPLAY(&Pci)){
		Status = EFI_SUCCESS;
		//
		// If this is an Intel graphics controller,
		// go further check RemainingDevicePath validation
		//
		if (RemainingDevicePath != NULL) {
		  Node = (EFI_DEV_PATH *) RemainingDevicePath;
		  //
		  // Check if RemainingDevicePath is the End of Device Path Node, 
		  // if yes, return EFI_SUCCESS
		  //
		  if (!IsDevicePathEnd (Node)) {
		    //
		    // If RemainingDevicePath isn't the End of Device Path Node,
		    // check its validation
		    //
		    if (Node->DevPath.Type != ACPI_DEVICE_PATH ||
		        Node->DevPath.SubType != ACPI_ADR_DP ||
		        DevicePathNodeLength(&Node->DevPath) != sizeof(ACPI_ADR_DEVICE_PATH)) {
		      Status = EFI_UNSUPPORTED;
		    }
		  }
		}
		if(Status==EFI_SUCCESS){
			DebugPrint(EFI_D_ERROR,"i915: found device %04x-%04x %p\n",Pci.Hdr.VendorId,Pci.Hdr.DeviceId,RemainingDevicePath);
			//DebugPrint(EFI_D_ERROR,"i915: bars %08x %08x %08x %08x\n",Pci.Device.Bar[0],Pci.Device.Bar[1],Pci.Device.Bar[2],Pci.Device.Bar[3]);
			//Status=EFI_UNSUPPORTED;
		}
	}
	
Done:
	gBS->CloseProtocol (
	      Controller,
	      &gEfiPciIoProtocolGuid,
	      This->DriverBindingHandle,
	      Controller
	      );
	return Status;
}

EFI_DRIVER_BINDING_PROTOCOL gi915DriverBinding = {
  i915ControllerDriverSupported,
  i915ControllerDriverStart,
  i915ControllerDriverStop,
  0x10,
  NULL,
  NULL
};

GLOBAL_REMOVE_IF_UNREFERENCED EFI_UNICODE_STRING_TABLE mi915DriverNameTable[] = {
  { "eng;en", L"i915 Driver" },
  { NULL , NULL }
};

GLOBAL_REMOVE_IF_UNREFERENCED EFI_UNICODE_STRING_TABLE mi915ControllerNameTable[] = {
  { "eng;en", L"i915 PCI Thing" },
  { NULL , NULL }
};

GLOBAL_REMOVE_IF_UNREFERENCED extern EFI_COMPONENT_NAME_PROTOCOL  gi915ComponentName;

EFI_STATUS
EFIAPI
i915ComponentNameGetDriverName (
  IN  EFI_COMPONENT_NAME_PROTOCOL  *This,
  IN  CHAR8                        *Language,
  OUT CHAR16                       **DriverName
  )
{
  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           mi915DriverNameTable,
           DriverName,
           (BOOLEAN)(This == &gi915ComponentName)
           );
}

EFI_STATUS
EFIAPI
i915ComponentNameGetControllerName (
  IN  EFI_COMPONENT_NAME_PROTOCOL                     *This,
  IN  EFI_HANDLE                                      ControllerHandle,
  IN  EFI_HANDLE                                      ChildHandle        OPTIONAL,
  IN  CHAR8                                           *Language,
  OUT CHAR16                                          **ControllerName
  )
{
  EFI_STATUS                      Status;

  //
  // This is a device driver, so ChildHandle must be NULL.
  //
  if (ChildHandle != NULL) {
    return EFI_UNSUPPORTED;
  }

  //
  // Make sure this driver is currently managing ControllHandle
  //
  Status = EfiTestManagedDevice (
             ControllerHandle,
             gi915DriverBinding.DriverBindingHandle,
             &gEfiPciIoProtocolGuid
             );
  if (EFI_ERROR (Status)) {
    return Status;
  }

  //
  // Get the Cirrus Logic 5430's Device structure
  //
  return LookupUnicodeString2 (
           Language,
           This->SupportedLanguages,
           mi915ControllerNameTable,
           ControllerName,
           (BOOLEAN)(This == &gi915ComponentName)
           );
}

GLOBAL_REMOVE_IF_UNREFERENCED EFI_COMPONENT_NAME_PROTOCOL  gi915ComponentName = {
  i915ComponentNameGetDriverName,
  i915ComponentNameGetControllerName,
  "eng"
};

GLOBAL_REMOVE_IF_UNREFERENCED EFI_COMPONENT_NAME2_PROTOCOL gi915ComponentName2 = {
  (EFI_COMPONENT_NAME2_GET_DRIVER_NAME) i915ComponentNameGetDriverName,
  (EFI_COMPONENT_NAME2_GET_CONTROLLER_NAME) i915ComponentNameGetControllerName,
  "en"
};

EFI_DRIVER_SUPPORTED_EFI_VERSION_PROTOCOL gi915SupportedEfiVersion = {
  sizeof (EFI_DRIVER_SUPPORTED_EFI_VERSION_PROTOCOL), // Size of Protocol structure.
  0                                                   // Version number to be filled at start up.
};

EFI_STATUS EFIAPI efi_main (
  IN EFI_HANDLE           ImageHandle,
  IN EFI_SYSTEM_TABLE     *SystemTable
  )
{
	////////////
	DebugPrint (EFI_D_ERROR, "Driver starts!\n");
	EFI_STATUS Status;
	Status = EfiLibInstallDriverBindingComponentName2 (
	           ImageHandle,
	           SystemTable,
	           &gi915DriverBinding,
	           ImageHandle,
	           &gi915ComponentName,
	           &gi915ComponentName2
	           );
	ASSERT_EFI_ERROR (Status);
	
	gi915SupportedEfiVersion.FirmwareVersion = PcdGet32 (PcdDriverSupportedEfiVersion);
	Status = gBS->InstallMultipleProtocolInterfaces (
	                &ImageHandle,
	                &gEfiDriverSupportedEfiVersionProtocolGuid,
	                &gi915SupportedEfiVersion,
	                NULL
	                );
	ASSERT_EFI_ERROR (Status);
	
	return EFI_SUCCESS;
}
