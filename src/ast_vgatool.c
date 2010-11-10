/*
 * Copyright (c) 2005 ASPEED Technology Inc.
 *
 * Permission to use, copy, modify, distribute, and sell this software and its
 * documentation for any purpose is hereby granted without fee, provided that
 * the above copyright notice appear in all copies and that both that
 * copyright notice and this permission notice appear in supporting
 * documentation, and that the name of the authors not be used in
 * advertising or publicity pertaining to distribution of the software without
 * specific, written prior permission.  The authors makes no representations
 * about the suitability of this software for any purpose.  It is provided
 * "as is" without express or implied warranty.
 *
 * THE AUTHORS DISCLAIMS ALL WARRANTIES WITH REGARD TO THIS SOFTWARE,
 * INCLUDING ALL IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS, IN NO
 * EVENT SHALL THE AUTHORS BE LIABLE FOR ANY SPECIAL, INDIRECT OR
 * CONSEQUENTIAL DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE,
 * DATA OR PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 * TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 * PERFORMANCE OF THIS SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif
#include "xf86.h"
#include "xf86_OSproc.h"
#include "xf86cmap.h"
#include "compiler.h"
#include "mibstore.h"
#include "vgaHW.h"
#include "mipointer.h"
#include "micmap.h"

#include "fb.h"
#include "regionstr.h"
#include "xf86xv.h"
#include <X11/extensions/Xv.h>
#include "vbe.h"

#include "xf86PciInfo.h"
#include "xf86Pci.h"

/* framebuffer offscreen manager */
#include "xf86fbman.h"

/* include xaa includes */
#include "xaa.h"
#include "xaarop.h"

/* H/W cursor support */
#include "xf86Cursor.h"

/* Driver specific headers */
#include "ast.h"

/* Prototype type declaration*/
void vASTOpenKey(ScrnInfoPtr pScrn);
Bool bASTRegInit(ScrnInfoPtr pScrn);
void GetDRAMInfo(ScrnInfoPtr pScrn);
ULONG GetVRAMInfo(ScrnInfoPtr pScrn);
ULONG GetMaxDCLK(ScrnInfoPtr pScrn);
void GetChipType(ScrnInfoPtr pScrn);
void vAST1000DisplayOn(ASTRecPtr pAST);
void vAST1000DisplayOff(ASTRecPtr pAST);
void ASTBlankScreen(ScrnInfoPtr pScrn, Bool unblack);
void vSetStartAddressCRT1(ASTRecPtr pAST, ULONG base);
void vASTLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices, LOCO *colors, VisualPtr pVisual);
void ASTDisplayPowerManagementSet(ScrnInfoPtr pScrn, int PowerManagementMode, int flags);
Bool GetVGA2EDID(ScrnInfoPtr pScrn, unsigned char *pEDIDBuffer);
void vInitDRAMReg(ScrnInfoPtr pScrn);
Bool bIsVGAEnabled(ScrnInfoPtr pScrn);
Bool InitVGA(ScrnInfoPtr pScrn, ULONG Flags);
Bool GetVGAEDID(ScrnInfoPtr pScrn, unsigned char *pEDIDBuffer);
Bool bInitAST1180(ScrnInfoPtr pScrn);
void GetAST1180DRAMInfo(ScrnInfoPtr pScrn);

void
vASTOpenKey(ScrnInfoPtr pScrn)
{   	
   ASTRecPtr pAST = ASTPTR(pScrn);
   
   SetIndexReg(CRTC_PORT,0x80, 0xA8);     
   
}

Bool
bASTRegInit(ScrnInfoPtr pScrn)
{
   ASTRecPtr pAST = ASTPTR(pScrn);

   /* Enable MMIO */
   SetIndexRegMask(CRTC_PORT,0xA1, 0xFF, 0x04);

   return (TRUE);
   	
}

void
GetDRAMInfo(ScrnInfoPtr pScrn)
{
    ASTRecPtr pAST = ASTPTR(pScrn);
    ULONG ulRefPLL, ulDeNumerator, ulNumerator, ulDivider;    
    ULONG ulData, ulData2;
    
    *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
    *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;
                    
    *(ULONG *) (pAST->MMIOVirtualAddr + 0x10000) = 0xFC600309;
    do {
       ; 	
    } while (*(volatile ULONG *) (pAST->MMIOVirtualAddr + 0x10000) != 0x01);
    
    ulData = *(volatile ULONG *) (pAST->MMIOVirtualAddr + 0x10004);
    
    /* Get BusWidth */
    if (ulData & 0x40)
       pAST->ulDRAMBusWidth = 16;
    else    
       pAST->ulDRAMBusWidth = 32;
    
    /* Get DRAM Type */
    if (pAST->jChipType == AST2300)
    {
        switch (ulData & 0x03)
        {
        case 0x00:
            pAST->jDRAMType = DRAMTYPE_512Mx16;
            break;            
        default:    
        case 0x01:
            pAST->jDRAMType = DRAMTYPE_1Gx16;
            break;                        
        case 0x02:
            pAST->jDRAMType = DRAMTYPE_2Gx16;
            break;                        
        case 0x03:	
            pAST->jDRAMType = DRAMTYPE_4Gx16;
            break;                        
        }	        	
    }
    else
    {
        switch (ulData & 0x0C)
        {
        case 0x00:	
        case 0x04:
            pAST->jDRAMType = DRAMTYPE_512Mx16;
            break;        
    
        case 0x08:
            if (ulData & 0x40)		/* 16bits */
                pAST->jDRAMType = DRAMTYPE_1Gx16;
            else			/* 32bits */
                pAST->jDRAMType = DRAMTYPE_512Mx32;
            break;
        
        case 0x0C:
            pAST->jDRAMType = DRAMTYPE_1Gx32;
            break;
        }
    }	
                    
    /* Get MCLK */
    ulData = *(ULONG *) (pAST->MMIOVirtualAddr + 0x10120);       
    ulData2 = *(ULONG *) (pAST->MMIOVirtualAddr + 0x10170);       
    if (ulData2 & 0x2000)
        ulRefPLL = 14318;
    else
        ulRefPLL = 12000;
        
    ulDeNumerator = ulData & 0x1F;
    ulNumerator = (ulData & 0x3FE0) >> 5;
            
    ulData = (ulData & 0xC000) >> 14;        
    switch (ulData)
    {
    case 0x03:
        ulDivider = 0x04;
        break;
    case 0x02:
    case 0x01:        
        ulDivider = 0x02;
        break;
    default:
        ulDivider = 0x01;                         
    }        
    pAST->ulMCLK = ulRefPLL * (ulNumerator + 2) / ((ulDeNumerator + 2) * ulDivider * 1000);                    
      	
} /* GetDRAMInfo */

ULONG
GetVRAMInfo(ScrnInfoPtr pScrn)
{
   ASTRecPtr pAST = ASTPTR(pScrn);
   UCHAR jReg;

   vASTOpenKey(pScrn);

   GetIndexRegMask(CRTC_PORT, 0xAA, 0xFF, jReg);  

   switch (jReg & 0x03)
   {
   case 0x00:
       return (VIDEOMEM_SIZE_08M);           
   case 0x01:
       return (VIDEOMEM_SIZE_16M);                  
   case 0x02:
       return (VIDEOMEM_SIZE_32M);                 
   case 0x03:	
       return (VIDEOMEM_SIZE_64M); 
   }                     
 
   return (DEFAULT_VIDEOMEM_SIZE);
   	
}

ULONG
GetMaxDCLK(ScrnInfoPtr pScrn)
{
   ASTRecPtr pAST = ASTPTR(pScrn);
   UCHAR jReg;
   ULONG ulDRAMBusWidth, ulMCLK, ulDRAMBandwidth, ActualDRAMBandwidth, DRAMEfficiency = 500;
   ULONG ulDCLK;

   ulMCLK = pAST->ulMCLK;
   ulDRAMBusWidth = pAST->ulDRAMBusWidth;       		
   
   /* Get Bandwidth */
   /* Modify DARM utilization to 60% for AST1100/2100 16bits DRAM, ycchen@032508 */
   if ( ((pAST->jChipType == AST2100) || (pAST->jChipType == AST1100) || (pAST->jChipType == AST2200) || (pAST->jChipType == AST2150)) && (ulDRAMBusWidth == 16) )
       DRAMEfficiency = 600;
   else if (pAST->jChipType == AST2300)
       DRAMEfficiency = 400;   
   ulDRAMBandwidth = ulMCLK * ulDRAMBusWidth * 2 / 8;
   ActualDRAMBandwidth = ulDRAMBandwidth * DRAMEfficiency / 1000;
   
   /* Get Max DCLK */
   if (pAST->jChipType == AST1180)
   {
       ulDCLK = ActualDRAMBandwidth / ((pScrn->bitsPerPixel+1) / 8);
   }
   else
   {	   
       /* Fixed Fixed KVM + CRT threshold issue on AST2100 8bpp modes, ycchen@100708 */    
       GetIndexRegMask(CRTC_PORT, 0xD0, 0xFF, jReg);  
       if ((jReg & 0x08) && (pAST->jChipType == AST2000))                     
           ulDCLK = ActualDRAMBandwidth / ((pScrn->bitsPerPixel+1+16) / 8);	
       else if ((jReg & 0x08) && (pScrn->bitsPerPixel == 8))
           ulDCLK = ActualDRAMBandwidth / ((pScrn->bitsPerPixel+1+24) / 8);	       
       else    
           ulDCLK = ActualDRAMBandwidth / ((pScrn->bitsPerPixel+1) / 8);	   
   }

   /* Add for AST2100, ycchen@061807 */
   if ((pAST->jChipType == AST2100) || (pAST->jChipType == AST2200) || (pAST->jChipType == AST2300) || (pAST->jChipType == AST1180) )
   {
       if (ulDCLK > 200) ulDCLK = 200;
   }
   else
   {
       if (ulDCLK > 165) ulDCLK = 165;       
   }
    
   return(ulDCLK);
   
}

void
GetChipType(ScrnInfoPtr pScrn)
{
   ASTRecPtr pAST = ASTPTR(pScrn);
   ULONG ulData;
   UCHAR jReg;
   
   pAST->jChipType = AST2100;

   *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
   *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;        

   ulData = *(ULONG *) (pAST->MMIOVirtualAddr + 0x1207c);       
   
#if 0   
   if ((ulData & 0x0300) == 0x0200)
       pAST->jChipType = AST1100;   
#endif       
   switch (ulData & 0x0300)
   {
   case 0x0200:
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "AST1100 Detected.\n");
       pAST->jChipType = AST1100;   
       break;
   case 0x0100:
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "AST2200 Detected.\n");   
       pAST->jChipType = AST2200;   
       break;
   case 0x0000:
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "AST2150 Detected.\n");   
       pAST->jChipType = AST2150;   
       break;
   default:           
       xf86DrvMsg(pScrn->scrnIndex, X_INFO, "AST2100 Detected.\n");   
       pAST->jChipType = AST2100;
   }	
       
   /* VGA2 Clone Support */    
   GetIndexRegMask(CRTC_PORT, 0x90, 0xFF, jReg);
   if (jReg & 0x10)
       pAST->VGA2Clone = TRUE;
          
}

void
vSetStartAddressCRT1(ASTRecPtr pAST, ULONG base)
{
    ULONG addr;
    	
    if (pAST->jChipType == AST1180)
    {
        addr = pAST->ulVRAMBase + base;
        WriteAST1180SOC(AST1180_GFX_BASE + AST1180_VGA1_STARTADDR, addr);                        	
    }
    else
    {
        addr = base >> 2;			/* DW unit */
    			
        SetIndexReg(CRTC_PORT,0x0D, (UCHAR) (addr & 0xFF));
        SetIndexReg(CRTC_PORT,0x0C, (UCHAR) ((addr >> 8) & 0xFF));
        SetIndexReg(CRTC_PORT,0xAF, (UCHAR) ((addr >> 16) & 0xFF));
    }    

}

void
vAST1000DisplayOff(ASTRecPtr pAST)
{
    ULONG ulData;
    	
    if (pAST->jChipType == AST1180)
    {
        ReadAST1180SOC(AST1180_GFX_BASE + AST1180_VGA1_CTRL, ulData);                
        ulData |= 0x00100000;    
        WriteAST1180SOC(AST1180_GFX_BASE + AST1180_VGA1_CTRL, ulData);                
    }
    else    
        SetIndexRegMask(SEQ_PORT,0x01, 0xDF, 0x20);
}


void
vAST1000DisplayOn(ASTRecPtr pAST)
{
   ULONG ulData;
	
    if (pAST->jChipType == AST1180)
    {
        ReadAST1180SOC(AST1180_GFX_BASE + AST1180_VGA1_CTRL, ulData);                
        ulData &= 0xFFEFFFFF;    
        WriteAST1180SOC(AST1180_GFX_BASE + AST1180_VGA1_CTRL, ulData);                
    }
    else
        SetIndexRegMask(SEQ_PORT,0x01, 0xDF, 0x00);
}	

void ASTBlankScreen(ScrnInfoPtr pScrn, Bool unblack)
{
   ASTRecPtr pAST;

   pAST = ASTPTR(pScrn);
	
    if (unblack)
        vAST1000DisplayOn(pAST);
    else
        vAST1000DisplayOff(pAST);	
} 

void
vASTLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices, LOCO *colors,
               VisualPtr pVisual)
{
	
    ASTRecPtr  pAST = ASTPTR(pScrn);
    int     i, j, index;
    UCHAR DACIndex, DACR, DACG, DACB;
  
    switch (pScrn->bitsPerPixel) {
    case 15:
        for(i=0; i<numColors; i++) {
            index = indices[i];
	    for(j=0; j<8; j++) {
                DACIndex = (index * 8) + j;
                DACR = colors[index].red << (8- pScrn->rgbBits);
                DACG = colors[index].green << (8- pScrn->rgbBits);
                DACB = colors[index].blue << (8- pScrn->rgbBits);
                         
                VGA_LOAD_PALETTE_INDEX (DACIndex, DACR, DACG, DACB);                         
	    }
        }
        break;
        
    case 16:
        for(i=0; i<numColors; i++) {
            index = indices[i];
	    for(j=0; j<4; j++) {
                DACIndex = (index * 4) + j;
                DACR = colors[index/2].red << (8- pScrn->rgbBits);
                DACG = colors[index].green << (8- pScrn->rgbBits);
                DACB = colors[index/2].blue << (8- pScrn->rgbBits);
                         
                VGA_LOAD_PALETTE_INDEX (DACIndex, DACR, DACG, DACB);                         
	    }
        }
        break;
        
    case 24:
        for(i=0; i<numColors; i++) {
            index = indices[i];
            DACIndex = index;
            DACR = colors[index].red;
            DACG = colors[index].green;
            DACB = colors[index].blue;
                         
            VGA_LOAD_PALETTE_INDEX (DACIndex, DACR, DACG, DACB);                         
        }    
        break;
        
    default:
        for(i=0; i<numColors; i++) {
            index = indices[i];
            DACIndex = index;
            DACR = colors[index].red >> (8 - pScrn->rgbBits);
            DACG = colors[index].green >> (8 - pScrn->rgbBits);
            DACB = colors[index].blue >> (8 - pScrn->rgbBits);
                         
            VGA_LOAD_PALETTE_INDEX (DACIndex, DACR, DACG, DACB);                         
        }    

    } /* end of switch */
    
} /* end of vASTLoadPalette */

void
ASTDisplayPowerManagementSet(ScrnInfoPtr pScrn, int PowerManagementMode, int flags)
{
   ASTRecPtr pAST;
   UCHAR SEQ01, CRB6;
   ULONG ulData, ulTemp;
   
   pAST = ASTPTR(pScrn);
   SEQ01=CRB6=0;
   ulData = 0;

   vASTOpenKey(pScrn);

   switch (PowerManagementMode) {
   case DPMSModeOn:
      /* Screen: On; HSync: On, VSync: On */
      SEQ01 = 0x00;
      CRB6 = 0x00;
      ulData = 0x00000000;
      break;
   case DPMSModeStandby:
      /* Screen: Off; HSync: Off, VSync: On */
      SEQ01 = 0x20;
      CRB6  = 0x01;
      ulData = 0x00140000;      
      break;
   case DPMSModeSuspend:
      /* Screen: Off; HSync: On, VSync: Off */
      SEQ01 = 0x20;
      CRB6  = 0x02;
      ulData = 0x00180000;      
      break;
   case DPMSModeOff:
      /* Screen: Off; HSync: Off, VSync: Off */
      SEQ01 = 0x20;
      CRB6  = 0x03;
      ulData = 0x001C0000;            
      break;
   }

   if (pAST->jChipType == AST1180)
   {
       ReadAST1180SOC(AST1180_GFX_BASE + AST1180_VGA1_CTRL, ulTemp);                
       ulTemp &= 0xFFE3FFFF; 
       ulTemp |= ulData;  
       WriteAST1180SOC(AST1180_GFX_BASE + AST1180_VGA1_CTRL, ulTemp);                
   }
   else
   {  
       SetIndexRegMask(SEQ_PORT,0x01, 0xDF, SEQ01);
       SetIndexRegMask(CRTC_PORT,0xB6, 0xFC, CRB6);
   }    

}


#define I2C_BASE		0x1e780000
#define I2C_OFFSET		(0xA000 + 0x40 * 4)	/* port4 */
#define I2C_DEVICEADDR		0x0A0			/* slave addr */

#define I2C_BASE_AST1180	0x80fc0000
#define I2C_OFFSET_AS1180	(0xB000 + 0x40 * 2)	/* port2 */
#define I2C_DEVICEADDR_AST1180	0x0A0			/* slave addr */

Bool
GetVGA2EDID(ScrnInfoPtr pScrn, unsigned char *pEDIDBuffer)
{
    ASTRecPtr pAST = ASTPTR(pScrn);
    ULONG i, ulData;    
    UCHAR *pjEDID;
    ULONG base, deviceaddr;
    UCHAR *offset;
    
    pjEDID = pEDIDBuffer;

    if (pAST->jChipType == AST1180)
    {
        base   = I2C_BASE_AST1180;
        offset = pAST->MMIOVirtualAddr + 0x10000 + I2C_OFFSET_AS1180;
        deviceaddr = I2C_DEVICEADDR_AST1180;
    }
    else
    {
        base   = I2C_BASE;
        offset = pAST->MMIOVirtualAddr + 0x10000 + I2C_OFFSET;
        deviceaddr = I2C_DEVICEADDR;
    		
        /* SCU settings */
        *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
        *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;
        usleep(10000);
     
        *(ULONG *) (pAST->MMIOVirtualAddr + 0x12000) = 0x1688A8A8;    
        ulData = *(ULONG *) (pAST->MMIOVirtualAddr + 0x12004);
        ulData &= 0xfffffffb;
        *(ULONG *) (pAST->MMIOVirtualAddr + 0x12004) = ulData;    
        usleep(10000);
    }

    /* I2C settings */
    *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = base;
    *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;
    usleep(10000);
    
    /* I2C Start */
    *(ULONG *) (offset + 0x00) = 0x0;
    *(ULONG *) (offset + 0x04) = 0x77777355;
    *(ULONG *) (offset + 0x08) = 0x0;
    *(ULONG *) (offset + 0x10) = 0xffffffff;
    *(ULONG *) (offset + 0x00) = 0x1;
    *(ULONG *) (offset + 0x0C) = 0xAF;
    *(ULONG *) (offset + 0x20) = deviceaddr;
    *(ULONG *) (offset + 0x14) = 0x03;
    do {
        ulData = *(volatile ULONG *) (offset + 0x10);
    } while (!(ulData & 0x03));
    if (ulData & 0x02)				/* NACK */
        return (FALSE);
    *(ULONG *) (offset + 0x10) = 0xffffffff;
    *(ULONG *) (offset + 0x20) = (ULONG) 0;	/* Offset */
    *(ULONG *) (offset + 0x14) = 0x02;
    do {
        ulData = *(volatile ULONG *) (offset + 0x10);
    } while (!(ulData & 0x01));
    *(ULONG *) (offset + 0x10) = 0xffffffff;
    *(ULONG *) (offset + 0x20) = deviceaddr + 1;
    *(ULONG *) (offset + 0x14) = 0x03; 
    do {
        ulData = *(volatile ULONG *) (offset + 0x10);
    } while (!(ulData & 0x01));
    
    /* I2C Read */
    for (i=0; i<127; i++)
    {
        *(ULONG *) (offset + 0x10) = 0xffffffff;
        *(ULONG *) (offset + 0x0C) |= 0x10;
        *(ULONG *) (offset + 0x14) = 0x08;
        do {
            ulData = *(volatile ULONG *) (offset + 0x10);
        } while (!(ulData & 0x04));
        *(ULONG *) (offset + 0x10) = 0xffffffff;
        *(UCHAR *) (pjEDID++) = (UCHAR) ((*(ULONG *) (offset + 0x20) & 0xFF00) >> 8);        	
    }

    /* Read Last Byte */
    *(ULONG *) (offset + 0x10) = 0xffffffff;
    *(ULONG *) (offset + 0x0C) |= 0x10;
    *(ULONG *) (offset + 0x14) = 0x18;
    do {
        ulData = *(volatile ULONG *) (offset + 0x10);
    } while (!(ulData & 0x04));
    *(ULONG *) (offset + 0x10) = 0xffffffff;
    *(UCHAR *) (pjEDID++) = (UCHAR) ((*(ULONG *) (offset + 0x20) & 0xFF00) >> 8);        	

    /* I2C Stop	 */
    *(ULONG *) (offset + 0x10) = 0xffffffff;
    *(ULONG *) (offset + 0x14) = 0x20;
    do {
        ulData = *(volatile ULONG *) (offset + 0x10);
    } while (!(ulData & 0x10));
    *(ULONG *) (offset + 0x0C) &= 0xffffffef;        
    *(ULONG *) (offset + 0x10) = 0xffffffff;
    
    return (TRUE);

} /* GetVGA2EDID */

/* Init VGA */
Bool bIsVGAEnabled(ScrnInfoPtr pScrn)
{
    ASTRecPtr pAST;
    UCHAR ch;
    ULONG ulData;
   
    pAST = ASTPTR(pScrn);

    if (pAST->jChipType == AST1180)
    {
        WriteAST1180SOC(AST1180_MMC_BASE+0x00, 0xFC600309);	/* unlock */
        ReadAST1180SOC(AST1180_MMC_BASE+0x08, ulData);
        return (ulData);    	
    }	
    else    
    {
    	
        ch = GetReg(pAST->RelocateIO+0x43);

        if (ch)
        {
    	
            vASTOpenKey(pScrn);
        
            GetIndexRegMask(CRTC_PORT, 0xB6, 0xFF, ch);  
        
            return (ch & 0x04);
        }
    }
    
    return (0);	
}	

void vEnableVGA(ScrnInfoPtr pScrn)
{
    ASTRecPtr pAST;
   
    pAST = ASTPTR(pScrn);

    SetReg(pAST->RelocateIO+0x43, 0x01);
    SetReg(pAST->RelocateIO+0x42, 0x01);   
	
}	

UCHAR ExtRegInfo[] = {
    0x0F,
    0x04,
    0x1C,
    0xFF
};

UCHAR ExtRegInfo_AST2300A0[] = {
    0x0F,
    0x04,
    0x1C,
    0xFF
};

UCHAR ExtRegInfo_AST2300[] = {
    0x0F,
    0x04,
    0x1F,
    0xFF
};

void vSetDefExtReg(ScrnInfoPtr pScrn)
{
    ASTRecPtr pAST;
    UCHAR i, jIndex, jReg, *pjExtRegInfo;
   
    pAST = ASTPTR(pScrn);

    /* Reset Scratch */
    for (i=0x81; i<=0x8F; i++)
    {
        SetIndexReg(CRTC_PORT, i, 0x00);
    }

    /* Set Ext. Reg */
    if (pAST->jChipType == AST2300)
    {
       if (PCI_DEV_REVISION(pAST->PciInfo) > 0x20)
           pjExtRegInfo = ExtRegInfo_AST2300;
       else
           pjExtRegInfo = ExtRegInfo_AST2300A0;           
    }    
    else
        pjExtRegInfo = ExtRegInfo;

    jIndex = 0xA0;
    while (*(UCHAR *) (pjExtRegInfo) != 0xFF)
    {
        SetIndexRegMask(CRTC_PORT,jIndex, 0x00, *(UCHAR *) (pjExtRegInfo));
        jIndex++;
        pjExtRegInfo++;
    }

    /* disable standard IO/MEM decode if secondary */
    if (!xf86IsPrimaryPci(pAST->PciInfo))
        SetIndexRegMask(CRTC_PORT,0xA1, 0xFF, 0x03);    	

    /* Set Ext. Default */
    SetIndexRegMask(CRTC_PORT,0x8C, 0x00, 0x01);    	
    SetIndexRegMask(CRTC_PORT,0xB7, 0x00, 0x00);    	
    
    /* Enable RAMDAC for A1, ycchen@113005 */
    jReg = 0x04;
    if (pAST->jChipType == AST2300)
        jReg |= 0x20;
    SetIndexRegMask(CRTC_PORT,0xB6, 0xFF, jReg);    	
      	
}

typedef struct _AST_DRAMStruct {
	
    USHORT 	Index;
    ULONG	Data;
    
} AST_DRAMStruct, *PAST_DRAMStruct; 

AST_DRAMStruct AST2000DRAMTableData[] = {
    { 0x0108, 0x00000000 },
    { 0x0120, 0x00004a21 },
    { 0xFF00, 0x00000043 },   	
    { 0x0000, 0xFFFFFFFF },
    { 0x0004, 0x00000089 },
    { 0x0008, 0x22331353 },
    { 0x000C, 0x0d07000b },
    { 0x0010, 0x11113333 },
    { 0x0020, 0x00110350 },   
    { 0x0028, 0x1e0828f0 },
    { 0x0024, 0x00000001 },
    { 0x001C, 0x00000000 },
    { 0x0014, 0x00000003 },
    { 0xFF00, 0x00000043 },
    { 0x0018, 0x00000131 },
    { 0x0014, 0x00000001 },
    { 0xFF00, 0x00000043 },
    { 0x0018, 0x00000031 },
    { 0x0014, 0x00000001 },
    { 0xFF00, 0x00000043 },
    { 0x0028, 0x1e0828f1 },
    { 0x0024, 0x00000003 },
    { 0x002C, 0x1f0f28fb },
    { 0x0030, 0xFFFFFE01 },
    { 0xFFFF, 0xFFFFFFFF }
};

AST_DRAMStruct AST1100DRAMTableData[] = {	
    { 0x2000, 0x1688a8a8 },
    { 0x2020, 0x000041f0 },
    { 0xFF00, 0x00000043 },
    { 0x0000, 0xfc600309 },
    { 0x006C, 0x00909090 },
    { 0x0064, 0x00050000 },
    { 0x0004, 0x00000585 },
    { 0x0008, 0x0011030f },
    { 0x0010, 0x22201724 },
    { 0x0018, 0x1e29011a },
    { 0x0020, 0x00c82222 },
    { 0x0014, 0x01001523 },
    { 0x001C, 0x1024010d },
    { 0x0024, 0x00cb2522 },
    { 0x0038, 0xffffff82 },
    { 0x003C, 0x00000000 },
    { 0x0040, 0x00000000 },
    { 0x0044, 0x00000000 },
    { 0x0048, 0x00000000 },
    { 0x004C, 0x00000000 },
    { 0x0050, 0x00000000 },
    { 0x0054, 0x00000000 },
    { 0x0058, 0x00000000 },
    { 0x005C, 0x00000000 },
    { 0x0060, 0x032aa02a },
    { 0x0064, 0x002d3000 },
    { 0x0068, 0x00000000 },
    { 0x0070, 0x00000000 },
    { 0x0074, 0x00000000 },
    { 0x0078, 0x00000000 },
    { 0x007C, 0x00000000 },
    { 0x0034, 0x00000001 },
    { 0xFF00, 0x00000043 },
    { 0x002C, 0x00000732 },
    { 0x0030, 0x00000040 },
    { 0x0028, 0x00000005 },
    { 0x0028, 0x00000007 },
    { 0x0028, 0x00000003 },
    { 0x0028, 0x00000001 },
    { 0x000C, 0x00005a08 },
    { 0x002C, 0x00000632 },
    { 0x0028, 0x00000001 },
    { 0x0030, 0x000003c0 },
    { 0x0028, 0x00000003 },
    { 0x0030, 0x00000040 },
    { 0x0028, 0x00000003 },
    { 0x000C, 0x00005a21 },
    { 0x0034, 0x00007c03 },
    { 0x0120, 0x00004c41 },
    { 0xffff, 0xffffffff },
};

AST_DRAMStruct AST2100DRAMTableData[] = {	
    { 0x2000, 0x1688a8a8 },
    { 0x2020, 0x00004120 },
    { 0xFF00, 0x00000043 },
    { 0x0000, 0xfc600309 },
    { 0x006C, 0x00909090 },
    { 0x0064, 0x00070000 },
    { 0x0004, 0x00000489 },
    { 0x0008, 0x0011030f },
    { 0x0010, 0x32302926 },
    { 0x0018, 0x274c0122 },
    { 0x0020, 0x00ce2222 },
    { 0x0014, 0x01001523 },
    { 0x001C, 0x1024010d },
    { 0x0024, 0x00cb2522 },
    { 0x0038, 0xffffff82 },
    { 0x003C, 0x00000000 },
    { 0x0040, 0x00000000 },
    { 0x0044, 0x00000000 },
    { 0x0048, 0x00000000 },
    { 0x004C, 0x00000000 },
    { 0x0050, 0x00000000 },
    { 0x0054, 0x00000000 },
    { 0x0058, 0x00000000 },
    { 0x005C, 0x00000000 },
    { 0x0060, 0x0f2aa02a },
    { 0x0064, 0x003f3005 },
    { 0x0068, 0x02020202 },
    { 0x0070, 0x00000000 },
    { 0x0074, 0x00000000 },
    { 0x0078, 0x00000000 },
    { 0x007C, 0x00000000 },
    { 0x0034, 0x00000001 },
    { 0xFF00, 0x00000043 },
    { 0x002C, 0x00000942 },
    { 0x0030, 0x00000040 },
    { 0x0028, 0x00000005 },
    { 0x0028, 0x00000007 },
    { 0x0028, 0x00000003 },
    { 0x0028, 0x00000001 },
    { 0x000C, 0x00005a08 },
    { 0x002C, 0x00000842 },
    { 0x0028, 0x00000001 },
    { 0x0030, 0x000003c0 },
    { 0x0028, 0x00000003 },
    { 0x0030, 0x00000040 },
    { 0x0028, 0x00000003 },
    { 0x000C, 0x00005a21 },
    { 0x0034, 0x00007c03 },
    { 0x0120, 0x00005061 },
    { 0xffff, 0xffffffff },
};

void vInitDRAMReg(ScrnInfoPtr pScrn)
{
    AST_DRAMStruct *pjDRAMRegInfo;
    ASTRecPtr pAST = ASTPTR(pScrn);
    ULONG i, ulTemp, ulData;
    UCHAR jReg;

    GetIndexRegMask(CRTC_PORT, 0xD0, 0xFF, jReg);  
    
    if ((jReg & 0x80) == 0)			/* VGA only */
    {
    	if (pAST->jChipType == AST2000)
    	{
            pjDRAMRegInfo = AST2000DRAMTableData;
            
            *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
            *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;        
            *(ULONG *) (pAST->MMIOVirtualAddr + 0x10100) = 0xa8;

            do {
               ; 	
            } while (*(volatile ULONG *) (pAST->MMIOVirtualAddr + 0x10100) != 0xa8);
                
        }
    	else	/* AST2100/1100 */	
    	{           
    	    if ((pAST->jChipType == AST2100) || (pAST->jChipType == AST2200))
                pjDRAMRegInfo = AST2100DRAMTableData;
    	    else
                pjDRAMRegInfo = AST1100DRAMTableData;
    		
            *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
            *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;
            
            *(ULONG *) (pAST->MMIOVirtualAddr + 0x12000) = 0x1688A8A8;
            do {
               ; 	
            } while (*(volatile ULONG *) (pAST->MMIOVirtualAddr + 0x12000) != 0x01);
            
            *(ULONG *) (pAST->MMIOVirtualAddr + 0x10000) = 0xFC600309;
            do {
               ; 	
            } while (*(volatile ULONG *) (pAST->MMIOVirtualAddr + 0x10000) != 0x01);

        }
	
        while (pjDRAMRegInfo->Index != 0xFFFF)
        {
            if (pjDRAMRegInfo->Index == 0xFF00)			/* Delay function */
            {
            	for (i=0; i<15; i++)				
                    usleep(pjDRAMRegInfo->Data);
            }
            else if ( (pjDRAMRegInfo->Index == 0x0004) && (pAST->jChipType != AST2000) )
            {
            	ulData = pjDRAMRegInfo->Data;
            	
            	if (pAST->jDRAMType == DRAMTYPE_1Gx16)
            	    ulData = 0x00000d89;
            	else if (pAST->jDRAMType == DRAMTYPE_1Gx32)
            	    ulData = 0x00000c8d;
            	        
                ulTemp = *(ULONG *) (pAST->MMIOVirtualAddr + 0x12070);
                ulTemp &= 0x0000000C;
                ulTemp <<= 2;                
                *(ULONG *) (pAST->MMIOVirtualAddr + 0x10000 + pjDRAMRegInfo->Index) = (ulData | ulTemp);               
            }	                
            else
            {	           	           
                *(ULONG *) (pAST->MMIOVirtualAddr + 0x10000 + pjDRAMRegInfo->Index) = pjDRAMRegInfo->Data;
            }
            pjDRAMRegInfo++;            
        }

        switch (pAST->jChipType)
        {
        case AST2000:
            *(ULONG *) (pAST->MMIOVirtualAddr + 0x10140) |= 0x40;
            break;
            
        case AST1100:
        case AST2100:
        case AST2200:
        case AST2150:        
            ulTemp = *(ULONG *) (pAST->MMIOVirtualAddr + 0x1200c);
            *(ULONG *) (pAST->MMIOVirtualAddr + 0x1200c) = (ulTemp & 0xFFFFFFFD);

            *(ULONG *) (pAST->MMIOVirtualAddr + 0x12040) |= 0x40;
            break;
        }
	            	
    } /* Init DRAM */
    
    /* wait ready */
    do {
        GetIndexRegMask(CRTC_PORT, 0xD0, 0xFF, jReg);      	
    } while ((jReg & 0x40) == 0);
       
} /* vInitDRAMReg */

/* 
 * AST2300 DRAM settings modules
 */
#define	DDR3		0
#define	DDR2		1

typedef struct _AST2300DRAMParam {
    UCHAR	*pjMMIOVirtualAddress;
    ULONG	DRAM_Type;
    ULONG	DRAM_ChipID;
    ULONG	DRAM_Freq;
    ULONG       VRAM_Size;
    ULONG	ODT;			/* 0/75/150 */
    ULONG	WODT;			/* 0/40/60/120 */
    ULONG	RODT;

    ULONG	DRAM_CONFIG;
    ULONG	REG_PERIOD;    
    ULONG  	REG_MADJ;
    ULONG	REG_SADJ;
    ULONG	REG_MRS;
    ULONG	REG_EMRS;
    ULONG	REG_AC1;
    ULONG	REG_AC2;
    ULONG	REG_DQSIC;
    ULONG	REG_DRV;    
    ULONG	REG_IOZ;
    ULONG	REG_DQIDLY;
    ULONG	REG_FREQ;
    
} AST2300DRAMParam, *PAST2300DRAMParam;

__inline ULONG MIndwm(UCHAR *mmiobase, ULONG r)
{
    *(ULONG *) (mmiobase + 0xF004) = r & 0xFFFF0000;
    *(ULONG *) (mmiobase + 0xF000) = 0x1;

    return ( *(volatile ULONG *) (mmiobase + 0x10000 + (r & 0x0000FFFF)) );
    	
}

__inline void MOutdwm(UCHAR *mmiobase, ULONG r, ULONG v)
{
    
    *(ULONG *) (mmiobase + 0xF004) = r & 0xFFFF0000;
    *(ULONG *) (mmiobase + 0xF000) = 0x1;
    
    *(volatile ULONG *) (mmiobase + 0x10000 + (r & 0x0000FFFF)) = v;
}

/*
 * DQSI DLL CBR Setting
 */
#define CBR_SIZE1            (256 - 1)
#define CBR_SIZE2            ((256 << 10) - 1)
#define CBR_PASSNUM          5
#define CBR_PASSNUM2         5
#define TIMEOUT              5000000

#define CBR_PATNUM           8

ULONG pattern[8] ={
0xFF00FF00,
0xCC33CC33,
0xAA55AA55,
0x88778877,
0x4D8223F3,
0x7EE02E83,
0x59E21CFB,
0x6ABB60C0};

int MMCTestBurst(PAST2300DRAMParam  param, ULONG datagen)
{
  ULONG data, timeout;
  UCHAR *mmiobase;	

  mmiobase = param->pjMMIOVirtualAddress;
  
  MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0070, 0x000000C1 | (datagen << 3));
  timeout = 0;
  do{
    data = MIndwm(mmiobase, 0x1E6E0070) & 0x3000;
    if(data & 0x2000){
      return(0);
    }
    if(++timeout > TIMEOUT){
      MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
      return(0);
    }
  }while(!data);
  MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
  return(1);
}

int MMCTestSingle(PAST2300DRAMParam  param, ULONG datagen)
{
  ULONG data, timeout;
  UCHAR *mmiobase;	

  mmiobase = param->pjMMIOVirtualAddress;

  MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0070, 0x000000C5 | (datagen << 3));
  timeout = 0;
  do{
    data = MIndwm(mmiobase, 0x1E6E0070) & 0x3000;
    if(data & 0x2000){
      return(0);
    }
    if(++timeout > TIMEOUT){
      MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
      return(0);
    }
  }while(!data);
  MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
  return(1);
}

int CBRTest(PAST2300DRAMParam  param, ULONG pattern)
{
  ULONG i,retry;
  ULONG data;
  UCHAR *mmiobase;	

  mmiobase = param->pjMMIOVirtualAddress;

  MOutdwm(mmiobase, 0x1E6E007C, pattern);
  if(!MMCTestBurst(param,0)) return(0);
  if(!MMCTestSingle(param,0)) return(0);
  if(!MMCTestBurst(param,1)) return(0);
  if(!MMCTestBurst(param,2)) return(0);
  if(!MMCTestBurst(param,3)) return(0);
  if(!MMCTestBurst(param,4)) return(0);
  if(!MMCTestBurst(param,5)) return(0);
  if(!MMCTestBurst(param,6)) return(0);
  if(!MMCTestBurst(param,7)) return(0);
  return(1);
}

int CBRScan(PAST2300DRAMParam  param, ULONG testsize)
{
  ULONG patcnt, loop;
  UCHAR *mmiobase;	

  mmiobase = param->pjMMIOVirtualAddress;

  MOutdwm(mmiobase, 0x1E6E0074, testsize);
  for(patcnt = 0;patcnt < CBR_PATNUM;patcnt++){
    for(loop = 0;loop < CBR_PASSNUM2;loop++){
      if(CBRTest(param,pattern[patcnt])){
        break;
      }
    }
    if(loop == CBR_PASSNUM2){
      return(0);
    }
  }
  return(1);
}

ULONG CBRTest2(PAST2300DRAMParam  param, ULONG pattern)
{
  ULONG errorbit = 0x0;
  UCHAR *mmiobase;	

  mmiobase = param->pjMMIOVirtualAddress;

  MOutdwm(mmiobase, 0x1E6E007C, pattern);
  if(!MMCTestBurst(param,0))  errorbit |= MIndwm(mmiobase, 0x1E6E0078);
  if(!MMCTestSingle(param,0)) errorbit |= MIndwm(mmiobase, 0x1E6E0078);
  return(errorbit);
}

int CBRScan2(PAST2300DRAMParam  param, ULONG testsize, ULONG errormask)
{
  ULONG patcnt, loop, data;
  UCHAR *mmiobase;	

  mmiobase = param->pjMMIOVirtualAddress;

  MOutdwm(mmiobase, 0x1E6E0074, testsize);
  for(patcnt = 0;patcnt < CBR_PATNUM;patcnt++){
    for(loop = 0;loop < CBR_PASSNUM2;loop++){
      if(!(data = CBRTest2(param, pattern[patcnt]) & errormask)){
        break;
      }
    }
    if(loop == CBR_PASSNUM2){
      return(0);
    }
  }
  return(1);
}

void finetuneDQI(PAST2300DRAMParam  param)
{
  ULONG gold_sadj, dqbit, dllmin, dllmax, dlli, errormask, data, cnt;
  UCHAR *mmiobase;	

  mmiobase = param->pjMMIOVirtualAddress;

  gold_sadj = (MIndwm(mmiobase, 0x1E6E0024) >> 16) & 0xff;

  errormask = 0x00010001;
  for(dqbit = 0;dqbit < 16;dqbit++){
    dllmin = 0xff;
    dllmax = 0x0;
    for(dlli = 0;dlli < 76;dlli++){
      MOutdwm(mmiobase, 0x1E6E0068, 0x00001400 | (dlli << 16) | (dlli << 24));
      /* Wait DQSI latch phase calibration */
      MOutdwm(mmiobase, 0x1E6E0074, 0x00000020);
      MOutdwm(mmiobase, 0x1E6E0070, 0x00000003);
      do{
        data = MIndwm(mmiobase, 0x1E6E0070);
      }while(!(data & 0x00001000));
      MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);

      for(cnt = 0;cnt < CBR_PASSNUM;cnt++){
        if(CBRScan2(param, CBR_SIZE1,errormask)){
          break;
        }
      }
      if(cnt < CBR_PASSNUM){
        if(dllmin > dlli){
          dllmin = dlli;
        }
        if(dllmax < dlli){
          dllmax = dlli;
        }
      }else if(dlli > dllmin){
        break;
      }
    }
    errormask = errormask << 1;
    dlli = (dllmin + dllmax) >> 1;
    if(gold_sadj >= dlli){
      dlli = (gold_sadj - dlli) >> 1;
      if(dlli > 3){
        dlli = 3;
      }else{
        dlli &= 0x3;
      }
    }else{
      dlli = (dlli - gold_sadj) >> 1;
      if(dlli > 4){
        dlli = 4;
      }else{
        dlli &= 0x7;
      }
      dlli = (0-dlli) & 0x7;
    }
    dlli = dlli << ((dqbit & 0x7) * 3);
    if(dqbit < 8){
      MOutdwm(mmiobase, 0x1E6E0080, MIndwm(mmiobase, 0x1E6E0080) | dlli);
    }else{
      MOutdwm(mmiobase, 0x1E6E0084, MIndwm(mmiobase, 0x1E6E0084) | dlli);
    }
  }
}    

void CBRDLL2(PAST2300DRAMParam  param)
{
  ULONG dllmin, dllmax, dlli, cnt, data, data2;
  UCHAR *mmiobase;	

  mmiobase = param->pjMMIOVirtualAddress;

  CBR_START:
  dllmin = 0xff;
  dllmax = 0x0;
  for(dlli = 0;dlli < 76;dlli++){
    MOutdwm(mmiobase, 0x1E6E0068, 0x00001300 | (dlli << 16) | (dlli << 24));
    /* Wait DQSI latch phase calibration */
    MOutdwm(mmiobase, 0x1E6E0074, 0x00000020);
    MOutdwm(mmiobase, 0x1E6E0070, 0x00000003);
    do{
      data = MIndwm(mmiobase, 0x1E6E0070);
    }while(!(data & 0x00001000));
    MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);

    for(cnt = 0;cnt < CBR_PASSNUM;cnt++){
      if(CBRScan(param, CBR_SIZE1)){
        break;
      }
    }
    if(cnt < CBR_PASSNUM){
      if(dllmin > dlli){
        dllmin = dlli;
      }
      if(dllmax < dlli){
        dllmax = dlli;
      }
    }else if(dlli > dllmin){
      break;
    }
  }
  if(dllmax != 0 && (dllmax-dllmin) < 10){
    goto CBR_START;
  }
  /*  dlli = dllmin + (int)((float)(dllmax - dllmin) * 0.5); */
  dlli = (dllmin + dllmax) >> 1;
  MOutdwm(mmiobase, 0x1E6E0068, 0x00001300 | (dlli << 16) | (dlli << 24));

  finetuneDQI(param);

  CBR_START2:
  dllmin = 0xff;
  dllmax = 0x0;
  for(dlli = 0;dlli < 76;dlli++){
    MOutdwm(mmiobase, 0x1E6E0068, 0x00001300 | (dlli << 16) | (dlli << 24));
    /* Wait DQSI latch phase calibration */
    MOutdwm(mmiobase, 0x1E6E0074, 0x00000020);
    MOutdwm(mmiobase, 0x1E6E0070, 0x00000003);
    do{
      data = MIndwm(mmiobase, 0x1E6E0070);
    }while(!(data & 0x00001000));
    MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);

    for(cnt = 0;cnt < CBR_PASSNUM;cnt++){
      if(CBRScan(param, CBR_SIZE2)){
        break;
      }
    }
    if(cnt < CBR_PASSNUM){
      if(dllmin > dlli){
        dllmin = dlli;
      }
      if(dllmax < dlli){
        dllmax = dlli;
      }
    }else if(dlli > dllmin){
      break;
    }
  }
  if(dllmax != 0 && (dllmax-dllmin) < 10){
    goto CBR_START2;
  }
  dlli = (dllmin + dllmax) >> 1;
  MOutdwm(mmiobase, 0x1E6E0068, 0x00001300 | (dlli << 16) | (dlli << 24));

  /* Set fine tune for DLL2 */
  data = (MIndwm(mmiobase, 0x1E6E0080) >> 24) & 0x1F;
  MOutdwm(mmiobase, 0x1E6E0024, 0xFF01 | (data << 1));
  data = (MIndwm(mmiobase, 0x1E6E0018) & 0xff80ffff) | (data << 16);
  MOutdwm(mmiobase, 0x1E6E0018, data);
  do{
    data  = MIndwm(mmiobase, 0x1E6E0088);
    data2 = MIndwm(mmiobase, 0x1E6E0088);
  }while(data != data2);
  data = (data >> 16) & 0x3FF;
  data2  = 12666667 / data;
  data2  += 1000000 / param->REG_PERIOD;
  data   *= 1000000 / param->REG_PERIOD;
  data2  = (data + (data2 >> 1)) / data2;
  data = MIndwm(mmiobase, 0x1E6E0024) & 0x80ff;
  data |= (data2 & 0x7F) << 8;
  MOutdwm(mmiobase, 0x1E6E0024, data);

  /* Wait DQSI latch phase calibration */
  MOutdwm(mmiobase, 0x1E6E0074, 0x00000020);
  MOutdwm(mmiobase, 0x1E6E0070, 0x00000003);
  do{
    data = MIndwm(mmiobase, 0x1E6E0070);
  }while(!(data & 0x00001000));
  MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0070, 0x00000003);
  do{
    data = MIndwm(mmiobase, 0x1E6E0070);
  }while(!(data & 0x00001000));
  MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
}

void GetDDR2Info(PAST2300DRAMParam param)
{
  UCHAR *mmiobase;
  
  mmiobase = param->pjMMIOVirtualAddress;
  
  MOutdwm(mmiobase, 0x1E6E2000, 0x1688A8A8);
  
  param->REG_MADJ 	= 0x00034C4C;
  param->REG_SADJ	= 0x00001400;
  param->REG_DRV      = 0x000000F0;
  param->REG_PERIOD   = param->DRAM_Freq;
  param->RODT		= 0;
	
  switch(param->DRAM_Freq){
    case 264 : MOutdwm(mmiobase, 0x1E6E2020, 0x0130);
               param->WODT   = 0;
               param->REG_AC1       = 0x22202513;
               param->REG_AC2       = 0x74117011;
               param->REG_DQSIC     = 0x000000B0;
               param->REG_MRS       = 0x00000842;
               param->REG_EMRS      = 0x00000000;
               param->REG_DRV       = 0x00000000;
               param->REG_IOZ       = 0x07000099;
               param->REG_DQIDLY    = 0x0000005C;
               param->REG_FREQ      = 0x00004DC0;
               break;
    case 336 : MOutdwm(mmiobase, 0x1E6E2020, 0x0190);
               param->WODT   = 0;
               param->REG_AC1       = 0x23202714;
               param->REG_AC2       = 0x86229016;
               param->REG_DQSIC     = 0x000000DF;
               param->REG_MRS       = 0x00000A52;
               param->REG_EMRS      = 0x00000000;
               param->REG_DRV       = 0x00000000;
               param->REG_IOZ       = 0x07000099;
               param->REG_DQIDLY    = 0x00000075;
               param->REG_FREQ      = 0x00004DC0;
               break;
    default:           
    case 408 : MOutdwm(mmiobase, 0x1E6E2020, 0x01F0);
               param->WODT   = 0;
               param->RODT   = 0;
               param->REG_AC1       = 0x33302715;
               param->REG_AC2       = 0xA722A01A;
               param->REG_DQSIC     = 0x0000010F;
               param->REG_MRS       = 0x00000A52;
               param->REG_EMRS      = 0x00000040;
               param->REG_DRV       = 0x00000000;
               param->REG_IOZ       = 0x07000099;
               param->REG_DQIDLY    = 0x0000008E;
               param->REG_FREQ      = 0x000050C0;
               break;
    case 456 : MOutdwm(mmiobase, 0x1E6E2020, 0x0230);
               param->WODT   = 0;
               param->REG_AC1       = 0x33302816;
               param->REG_AC2       = 0xB844B01D;
               param->REG_DQSIC     = 0x0000012F;
               param->REG_MRS       = 0x00000C72;
               param->REG_EMRS      = 0x00000000;
               param->REG_DRV       = 0x00000000;
               param->REG_IOZ       = 0x07000000;
               param->REG_DQIDLY    = 0x0000009F;
               param->REG_FREQ      = 0x000052C0;
               break;
    case 504 : MOutdwm(mmiobase, 0x1E6E2020, 0x0261);
               param->WODT   = 1;
               param->RODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x34302916;
               param->REG_AC2       = 0xC944D01F;
               param->REG_DQSIC     = 0x0000014F;
               param->REG_MRS       = 0x00000C72;
               param->REG_EMRS      = 0x00000040;
               param->REG_DRV       = 0x00000000;
               param->REG_IOZ       = 0x07000000;
               param->REG_DQIDLY    = 0x000000B0;
               param->REG_FREQ      = 0x000054C0;
               break;
    case 528 : MOutdwm(mmiobase, 0x1E6E2020, 0x0120);
               param->WODT   = 1;
               param->RODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x44403916;
               param->REG_AC2       = 0xD944D01F;
               param->REG_DQSIC     = 0x0000015F;
               param->REG_MRS       = 0x00000C72;
               param->REG_EMRS      = 0x00000040;
               param->REG_DRV       = 0x00000000;
               param->REG_IOZ       = 0x07000011;
               param->REG_DQIDLY    = 0x000000B9;
               param->REG_FREQ      = 0x000055C0;
               break;
    case 552 : MOutdwm(mmiobase, 0x1E6E2020, 0x02A1);
               param->WODT   = 1;
               param->RODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x44403916;
               param->REG_AC2       = 0xD944E01F;
               param->REG_DQSIC     = 0x0000016F;
               param->REG_MRS       = 0x00000C72;
               param->REG_EMRS      = 0x00000040;
               param->REG_DRV       = 0x00000000;
               param->REG_IOZ       = 0x07000000;
               param->REG_DQIDLY    = 0x000000C1;
               param->REG_FREQ      = 0x000057C0;
               break;
    case 576 : MOutdwm(mmiobase, 0x1E6E2020, 0x0140);
               param->WODT   = 1;
               param->RODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x44403916;
               param->REG_AC2       = 0xEA44E01F;
               param->REG_DQSIC     = 0x0000017F;
               param->REG_MRS       = 0x00000C72;
               param->REG_EMRS      = 0x00000040;
               param->REG_DRV       = 0x00000000;
               param->REG_IOZ       = 0x07000000;
               param->REG_DQIDLY    = 0x000000CA;
               param->REG_FREQ      = 0x000057C0;
               break;
  }

  switch (param->DRAM_ChipID)
  {
  case DRAMTYPE_512Mx16:	
      param->DRAM_CONFIG = 0x100;
      break;
  default:    
  case DRAMTYPE_1Gx16:	
      param->DRAM_CONFIG = 0x121;
      break;
   case DRAMTYPE_2Gx16:	
      param->DRAM_CONFIG = 0x122;
      break;
  case DRAMTYPE_4Gx16:	
      param->DRAM_CONFIG = 0x123;
      break;     
  }; /* switch size */
  
  switch (param->VRAM_Size)
  {
  default: 	
  case VIDEOMEM_SIZE_08M:
      param->DRAM_CONFIG |= 0x00;
      break;
  case VIDEOMEM_SIZE_16M:
      param->DRAM_CONFIG |= 0x04;
      break;
  case VIDEOMEM_SIZE_32M:
      param->DRAM_CONFIG |= 0x08;
      break;
  case VIDEOMEM_SIZE_64M:
      param->DRAM_CONFIG |= 0x0c;
      break;  	
  }
	
}

void GetDDR3Info(PAST2300DRAMParam param)
{
  UCHAR *mmiobase;
  
  mmiobase = param->pjMMIOVirtualAddress;
  
  param->REG_MADJ 	= 0x00034C4C;
  param->REG_SADJ	= 0x00001600;
  param->REG_DRV      = 0x000000F0;
  param->REG_PERIOD   = param->DRAM_Freq;
  param->RODT		= 0;

  MOutdwm(mmiobase, 0x1E6E2000, 0x1688A8A8);

  switch(param->DRAM_Freq){
    case 336 : MOutdwm(mmiobase, 0x1E6E2020, 0x0190);
               param->WODT   	      = 0;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x23202826;
               param->REG_AC2       = 0x85327513;
               param->REG_DQSIC     = 0x000000DF;
               param->REG_MRS       = 0x00001410;
               param->REG_EMRS      = 0x00000000;
               param->REG_IOZ       = 0x070000CC;
               param->REG_DQIDLY    = 0x00000075;
               param->REG_FREQ      = 0x00004DC0;
               break;
    default:           
    case 408 : MOutdwm(mmiobase, 0x1E6E2020, 0x01F0);
               param->WODT   = 0;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x33302826;
               param->REG_AC2       = 0xA6329516;
               param->REG_DQSIC     = 0x0000010F;
               param->REG_MRS       = 0x00001610;
               param->REG_EMRS      = 0x00000000;
               param->REG_IOZ       = 0x070000CC;
               param->REG_DQIDLY    = 0x0000008E;
               param->REG_FREQ      = 0x000050C0;
               break;
    case 456 : MOutdwm(mmiobase, 0x1E6E2020, 0x0230);
               param->WODT   = 0;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x33302937;
               param->REG_AC2       = 0xB7449519;
               param->REG_DQSIC     = 0x0000012F;
               param->REG_MRS       = 0x00081630;
               param->REG_EMRS      = 0x00000000;
               param->REG_IOZ       = 0x070000CC;
               param->REG_DQIDLY    = 0x0000009F;
               param->REG_FREQ      = 0x000052C0;
               break;
    case 504 : MOutdwm(mmiobase, 0x1E6E2020, 0x0270);
               param->WODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x33302A37;
               param->REG_AC2       = 0xC744A51C;
               param->REG_DQSIC     = 0x0000014F;
               param->REG_MRS       = 0x00081830;
               param->REG_IOZ       = 0x070000BB;
               param->REG_DQIDLY    = 0x000000B0;
               param->REG_FREQ      = 0x000054C0;
               break;
    case 528 : MOutdwm(mmiobase, 0x1E6E2020, 0x0290);
               param->WODT   = 1;
               param->RODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x33303A37;
               param->REG_AC2       = 0xD844B61D;
               param->REG_DQSIC     = 0x0000015F;
               param->REG_MRS       = 0x00081A30;
               param->REG_EMRS      = 0x00000040;
               param->REG_IOZ       = 0x070000BB;
               param->REG_DQIDLY    = 0x000000B9;
               param->REG_FREQ      = 0x000055C0;
               break;
    case 576 : MOutdwm(mmiobase, 0x1E6E2020, 0x0140);
               param->WODT   = 1;
               param->RODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x43403A27;
               param->REG_AC2       = 0xC955C51F;
               param->REG_DQSIC     = 0x0000017F;
               param->REG_MRS       = 0x00101A40;
               param->REG_EMRS      = 0x00000040;
               param->REG_IOZ       = 0x070000BB;
               param->REG_DQIDLY    = 0x000000CA;
               param->REG_FREQ      = 0x000057C0;
               break;
    case 600 : MOutdwm(mmiobase, 0x1E6E2020, 0x02E1);
               param->WODT   = 1;
               param->RODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x43403B27;
               param->REG_AC2       = 0xE955C51F;
               param->REG_DQSIC     = 0x0000018F;
               param->REG_MRS       = 0x00101C40;
               param->REG_EMRS      = 0x00000004;
               param->REG_IOZ       = 0x070000BB;
               param->REG_DQIDLY    = 0x000000D2;
               param->REG_FREQ      = 0x000058C0;
               break;
    case 624 : MOutdwm(mmiobase, 0x1E6E2020, 0x0160);
               param->WODT   = 1;
               param->RODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x43403B27;
               param->REG_AC2       = 0xF955C51F;
               param->REG_DQSIC     = 0x0000019F;
               param->REG_MRS       = 0x04101C40;
               param->REG_EMRS      = 0x00000040;
               param->REG_IOZ       = 0x070000BB;
               param->REG_DQIDLY    = 0x000000DA;
               param->REG_FREQ      = 0x000059C0;
               break;
    case 648 : MOutdwm(mmiobase, 0x1E6E2020, 0x0321);
               param->WODT   = 1;
               param->REG_SADJ      = 0x00001400;
               param->REG_AC1       = 0x43403B27;
               param->REG_AC2       = 0xFA55D51F;
               param->REG_DQSIC     = 0x000001AF;
               param->REG_MRS       = 0x00101C40;
               param->REG_IOZ       = 0x070000AA;
               param->REG_FREQ      = 0x00005AC0;
               param->REG_DQIDLY    = 0x000000E3;
               param->REG_SADJ      = 0x00001600;
               break;
  } /* switch freq */

  switch (param->DRAM_ChipID)
  {
  case DRAMTYPE_512Mx16:	
      param->DRAM_CONFIG = 0x130;
      break;
  default:    
  case DRAMTYPE_1Gx16:	
      param->DRAM_CONFIG = 0x131;
      break;
   case DRAMTYPE_2Gx16:	
      param->DRAM_CONFIG = 0x132;
      break;
  case DRAMTYPE_4Gx16:	
      param->DRAM_CONFIG = 0x133;
      break;     
  }; /* switch size */

  switch (param->VRAM_Size)
  {
  default: 	
  case VIDEOMEM_SIZE_08M:
      param->DRAM_CONFIG |= 0x00;
      break;
  case VIDEOMEM_SIZE_16M:
      param->DRAM_CONFIG |= 0x04;
      break;
  case VIDEOMEM_SIZE_32M:
      param->DRAM_CONFIG |= 0x08;
      break;
  case VIDEOMEM_SIZE_64M:
      param->DRAM_CONFIG |= 0x0c;
      break;  	
  }
  
}

void DDR2_Init(PAST2300DRAMParam param)
{
  ULONG data;
  UCHAR *mmiobase;
  
  mmiobase = param->pjMMIOVirtualAddress;

  MOutdwm(mmiobase, 0x1E6E0000, 0xFC600309);
  MOutdwm(mmiobase, 0x1E6E0018, 0x00000100);
  MOutdwm(mmiobase, 0x1E6E0024, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ);
  MOutdwm(mmiobase, 0x1E6E0068, param->REG_SADJ);
  usleep(10);                                               		/* Delay 2 us */
  MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ | 0x40000);

  MOutdwm(mmiobase, 0x1E6E0004, param->DRAM_CONFIG);
  MOutdwm(mmiobase, 0x1E6E0008, 0x90040f);
  MOutdwm(mmiobase, 0x1E6E0010, param->REG_AC1);
  MOutdwm(mmiobase, 0x1E6E0014, param->REG_AC2);
  MOutdwm(mmiobase, 0x1E6E0020, param->REG_DQSIC);
  MOutdwm(mmiobase, 0x1E6E0080, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0084, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0088, param->REG_DQIDLY);                   	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0018, 0x4040C130);
  MOutdwm(mmiobase, 0x1E6E0018, 0x20404330);                         	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0038, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0040, 0xFF808000);                         	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0044, 0x88848466);                         	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0048, 0x44440008);                         	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E004C, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0050, 0x80000000);
  MOutdwm(mmiobase, 0x1E6E0050, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0054, 0);
  if(param->RODT)
  {
      if (param->ODT == 75) 
          MOutdwm(mmiobase, 0x1E6E0060, param->REG_DRV | 0x5);
      else
          MOutdwm(mmiobase, 0x1E6E0060, param->REG_DRV | 0xA);
  }else
  {
    MOutdwm(mmiobase, 0x1E6E0060, param->REG_DRV);
  }
  /* Wait MCLK2X lock to MCLK */
  do{
    data = MIndwm(mmiobase, 0x1E6E001C);
  }while(!(data & 0x08000000));
  data = (data >> 8) & 0xff;
  if(data > 61 || data < 15){
    data = MIndwm(mmiobase, 0x1E6E0018) ^ 0x300;
    MOutdwm(mmiobase, 0x1E6E0018, data);
    MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ);
    usleep(10);                                                       /* Delay 2 us */
    data = data | 0x200;
    MOutdwm(mmiobase, 0x1E6E0018, data);
    MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ | 0x40000);
    do{
      data = MIndwm(mmiobase, 0x1E6E001C);
    }while(!(data & 0x08000000));
  }
  data = MIndwm(mmiobase, 0x1E6E0018) | 0xC00;
  MOutdwm(mmiobase, 0x1E6E0018, data);
  MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ | 0xC0000);
  MOutdwm(mmiobase, 0x1E6E006C, param->REG_IOZ);                     	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0074, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0078, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E007C, 0x00000000);
  /* Wait DQI delay lock */
  do{
    data = MIndwm(mmiobase, 0x1E6E0080);
  }while(!(data & 0x40000000));
  MOutdwm(mmiobase, 0x1E6E0034, 0x00000001);
  MOutdwm(mmiobase, 0x1E6E000C, 0x00000000);
  usleep(50);                                                         	/* Delay 400 us */
  /* Mode Register Setting */
  MOutdwm(mmiobase, 0x1E6E002C, param->REG_MRS | 0x100);
  MOutdwm(mmiobase, 0x1E6E0030, param->REG_EMRS);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000005);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000007);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000003);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000001);
  MOutdwm(mmiobase, 0x1E6E000C, 0x00005A08);
  MOutdwm(mmiobase, 0x1E6E002C, param->REG_MRS);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000001);
  MOutdwm(mmiobase, 0x1E6E0030, param->REG_EMRS | 0x380);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000003);
  MOutdwm(mmiobase, 0x1E6E0030, param->REG_EMRS);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000003);

  MOutdwm(mmiobase, 0x1E6E000C, 0x7FFF5A01);
  data = 0;
  if(param->WODT){
    data = 0x700;
  }
  if(param->RODT){
    data = data | 0x3000 | ((param->REG_AC2 & 0x60000) >> 3);
  }
  MOutdwm(mmiobase, 0x1E6E0034, data | 0x3);
  MOutdwm(mmiobase, 0x1E6E0120, param->REG_FREQ);

  /* Wait DQSI delay lock */
  do{
    data = MIndwm(mmiobase, 0x1E6E0020);
  }while(!(data & 0x00000800));
  /* Calibrate the DQSI delay */
  CBRDLL2(param);

}

void DDR3_Init(PAST2300DRAMParam param)
{
  ULONG data;
  UCHAR *mmiobase;
  
  mmiobase = param->pjMMIOVirtualAddress;

  MOutdwm(mmiobase, 0x1E6E0000, 0xFC600309);
  MOutdwm(mmiobase, 0x1E6E0018, 0x00000100);
  MOutdwm(mmiobase, 0x1E6E0024, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ);
  MOutdwm(mmiobase, 0x1E6E0068, param->REG_SADJ);
  usleep(10);                                                         	/* Delay 2 us */
  MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ | 0x40000);

  MOutdwm(mmiobase, 0x1E6E0004, param->DRAM_CONFIG);
  MOutdwm(mmiobase, 0x1E6E0008, 0x90040f);
  MOutdwm(mmiobase, 0x1E6E0010, param->REG_AC1);
  MOutdwm(mmiobase, 0x1E6E0014, param->REG_AC2);
  MOutdwm(mmiobase, 0x1E6E0020, param->REG_DQSIC);
  MOutdwm(mmiobase, 0x1E6E0080, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0084, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0088, param->REG_DQIDLY);                 	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0018, 0x4040C170);
  MOutdwm(mmiobase, 0x1E6E0018, 0x20404370);                         	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0038, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0040, 0xFF808000);                         	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0044, 0x88848466);                         	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0048, 0x44440008);                         	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E004C, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0050, 0x80000000);
  MOutdwm(mmiobase, 0x1E6E0050, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0054, 0);
  if(param->RODT)
  {
      if (param->ODT == 75)
          MOutdwm(mmiobase, 0x1E6E0060, param->REG_DRV | 0x5);
      else
          MOutdwm(mmiobase, 0x1E6E0060, param->REG_DRV | 0xA);      
  }
  else
  {
    MOutdwm(mmiobase, 0x1E6E0060, param->REG_DRV);
  }
  /* Wait MCLK2X lock to MCLK */
  do{
    data = MIndwm(mmiobase, 0x1E6E001C);
  }while(!(data & 0x08000000));
  data = (data >> 8) & 0xff;
  if(data > 61 || data < 15){
    data = MIndwm(mmiobase, 0x1E6E0018) ^ 0x300;
    MOutdwm(mmiobase, 0x1E6E0018, data);
    MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ);
    usleep(10);                                                	/* Delay 2 us */
    data = data | 0x200;
    MOutdwm(mmiobase, 0x1E6E0018, data);
    MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ | 0x40000);
    do{
      data = MIndwm(mmiobase, 0x1E6E001C);
    }while(!(data & 0x08000000));
  }
  data = MIndwm(mmiobase, 0x1E6E0018) | 0xC00;
  MOutdwm(mmiobase, 0x1E6E0018, data);
  MOutdwm(mmiobase, 0x1E6E0064, param->REG_MADJ | 0xC0000);
  MOutdwm(mmiobase, 0x1E6E006C, param->REG_IOZ);                     	/* fine tune required */
  MOutdwm(mmiobase, 0x1E6E0070, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0074, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E0078, 0x00000000);
  MOutdwm(mmiobase, 0x1E6E007C, 0x00000000);
  /* Wait DQI delay lock */
  do{
    data = MIndwm(mmiobase, 0x1E6E0080);
  }while(!(data & 0x40000000));
  MOutdwm(mmiobase, 0x1E6E0034, 0x00000001);
  MOutdwm(mmiobase, 0x1E6E000C, 0x00000040);
  usleep(50);                                                         	/* Delay 400 us */
  /* Mode Register Setting */
  MOutdwm(mmiobase, 0x1E6E002C, param->REG_MRS | 0x100);
  MOutdwm(mmiobase, 0x1E6E0030, param->REG_EMRS);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000005);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000007);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000003);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000001);
  MOutdwm(mmiobase, 0x1E6E002C, param->REG_MRS);
  MOutdwm(mmiobase, 0x1E6E000C, 0x00005A48);
  MOutdwm(mmiobase, 0x1E6E0028, 0x00000001);

  MOutdwm(mmiobase, 0x1E6E000C, 0x7FFF5A81);
  data = 0;
  if(param->WODT){
    data = 0x700;
  }
  if(param->RODT){
    data = data | 0x3000 | ((param->REG_AC2 & 0x60000) >> 3);
  }
  MOutdwm(mmiobase, 0x1E6E0034, data | 0x3);

  /* Wait DQSI delay lock */
  do{
    data = MIndwm(mmiobase, 0x1E6E0020);
  }while(!(data & 0x00000800));
  /* Calibrate the DQSI delay */
  CBRDLL2(param);

  MOutdwm(mmiobase, 0x1E6E0120, param->REG_FREQ);

}

void vInitAST2300DRAMReg(ScrnInfoPtr pScrn)
{
    ASTRecPtr pAST = ASTPTR(pScrn);
    AST2300DRAMParam param;
    ULONG i, ulTemp;
    UCHAR jReg;

    GetIndexRegMask(CRTC_PORT, 0xD0, 0xFF, jReg);  
    
    if ((jReg & 0x80) == 0)			/* VGA only */
    {
        *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
        *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;
       
        *(ULONG *) (pAST->MMIOVirtualAddr + 0x12000) = 0x1688A8A8;
        do {
           ; 	
        } while (*(volatile ULONG *) (pAST->MMIOVirtualAddr + 0x12000) != 0x01);
       
        *(ULONG *) (pAST->MMIOVirtualAddr + 0x10000) = 0xFC600309;
        do {
          ; 	
        } while (*(volatile ULONG *) (pAST->MMIOVirtualAddr + 0x10000) != 0x01);
    	
        param.pjMMIOVirtualAddress = pAST->MMIOVirtualAddr;
        param.DRAM_Type = DDR3;			/* DDR3 */
        ulTemp = MIndwm(param.pjMMIOVirtualAddress, 0x1E6E2070);    
        if (ulTemp & 0x01000000)
            param.DRAM_Type = DDR2;		/* DDR2 */
        param.DRAM_ChipID = (ULONG) pAST->jDRAMType;
        param.DRAM_Freq = pAST->ulMCLK;
        param.VRAM_Size = pAST->ulVRAMSize;
    
        if (param.DRAM_Type == DDR3)
        {
            GetDDR3Info(&param);	
            DDR3_Init(&param);
        }
        else
        {
            GetDDR2Info(&param);
            DDR2_Init(&param);        	    	
        }
        
        ulTemp  = MIndwm(param.pjMMIOVirtualAddress, 0x1E6E2040);    
        MOutdwm(param.pjMMIOVirtualAddress, 0x1E6E2040, ulTemp | 0x40);
    }

    /* wait ready */
    do {
        GetIndexRegMask(CRTC_PORT, 0xD0, 0xFF, jReg);      	
    } while ((jReg & 0x40) == 0);
        	
} /* vInitAST2300DRAMReg */	

void vGetDefaultSettings(ScrnInfoPtr pScrn)
{
    ASTRecPtr pAST = ASTPTR(pScrn);
    ULONG ulData;	
	
    if (pAST->jChipType == AST2300)
    {
        *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
        *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;
        ulData = *(ULONG *) (pAST->MMIOVirtualAddr + 0x12070);
        switch (ulData & 0x18000000)
        {
        case 0x00000000:
            pAST->jDRAMType = DRAMTYPE_512Mx16;    	        
            break;        
        case 0x08000000:
            pAST->jDRAMType = DRAMTYPE_1Gx16;    	        
            break;
        case 0x10000000:
            pAST->jDRAMType = DRAMTYPE_2Gx16;    	        
            break;        
        case 0x18000000:
            pAST->jDRAMType = DRAMTYPE_4Gx16;    	        
            break;        	
        }	
    }	
    else if ((pAST->jChipType == AST2100) || (pAST->jChipType == AST2200))
    {
        pAST->jDRAMType = DRAMTYPE_512Mx32;    	    	
    }
    else if ((pAST->jChipType == AST1100) || (pAST->jChipType == AST2150))
    {
        pAST->jDRAMType = DRAMTYPE_1Gx16;    	
    }	
	
} /* vGetDefaultSettings */	

/*
 * Flags: 0: POST init
 *        1: resume from power management
 */
Bool InitVGA(ScrnInfoPtr pScrn, ULONG Flags)
{
   ASTRecPtr pAST;
   ULONG ulData;

   pAST = ASTPTR(pScrn);

   {
       /* Enable PCI */
       PCI_READ_LONG(pAST->PciInfo, &ulData, 0x04);
       ulData |= 0x03;
       PCI_WRITE_LONG(pAST->PciInfo, ulData, 0x04);       

       /* Enable VGA */
       vEnableVGA(pScrn);
       
       vASTOpenKey(pScrn);
       vSetDefExtReg(pScrn);

       if (Flags == 0)
           vGetDefaultSettings(pScrn);
                  
       if (pAST->jChipType == AST2300)
           vInitAST2300DRAMReg(pScrn);
       else
           vInitDRAMReg(pScrn);      
             
   }

   return (TRUE);	
} /* Init VGA */

/* Get EDID */
void 
I2CWriteClock(ASTRecPtr pAST, UCHAR data)
{
    UCHAR       ujCRB7, jtemp;
    ULONG	i;
       
    for (i=0;i<0x10000; i++)
    {
        ujCRB7 = ((data & 0x01) ? 0:1);			/* low active */
        SetIndexRegMask(CRTC_PORT, 0xB7, 0xFE, ujCRB7);
        GetIndexRegMask(CRTC_PORT, 0xB7, 0x01, jtemp);
        if (ujCRB7 == jtemp) break;
    }
        
}

void 
I2CWriteData(ASTRecPtr pAST, UCHAR data)
{
    UCHAR       volatile ujCRB7, jtemp;
    ULONG	i;
    
    for (i=0;i<0x1000; i++)
    {        
        ujCRB7 = ((data & 0x01) ? 0:1) << 2;		/* low active */
        SetIndexRegMask(CRTC_PORT, 0xB7, 0xFB, ujCRB7);
        GetIndexRegMask(CRTC_PORT, 0xB7, 0x04, jtemp);
        if (ujCRB7 == jtemp) break;        
    }
    
}

Bool 
I2CReadClock(ASTRecPtr pAST)
{	
    UCHAR       volatile ujCRB7;
          
    GetIndexRegMask(CRTC_PORT, 0xB7, 0x10, ujCRB7);
    ujCRB7 >>= 4;
    
    return ((ujCRB7 & 0x01) ? 1:0);
}

Bool 
I2CReadData(ASTRecPtr pAST)
{	
    UCHAR	volatile ujCRB7;
    
    GetIndexRegMask(CRTC_PORT, 0xB7, 0x20, ujCRB7);
    ujCRB7 >>= 5;
    
    return ((ujCRB7 & 0x01) ? 1:0);

}


void
I2CDelay(ASTRecPtr pAST)
{
    ULONG 	i;
    UCHAR       jtemp;
             
    for (i=0;i<150;i++)
        jtemp = GetReg(SEQ_PORT);
       	
}
 
void 
I2CStart(ASTRecPtr pAST)
{    
    I2CWriteClock(pAST, 0x00);				/* Set Clk Low */
    I2CDelay(pAST);
    I2CWriteData(pAST, 0x01);				/* Set Data High */
    I2CDelay(pAST);    
    I2CWriteClock(pAST, 0x01);				/* Set Clk High */
    I2CDelay(pAST);    
    I2CWriteData(pAST, 0x00);				/* Set Data Low */
    I2CDelay(pAST);    
    I2CWriteClock(pAST, 0x01);				/* Set Clk High */
    I2CDelay(pAST);                    
}     

void 
I2CStop(ASTRecPtr pAST)
{
    I2CWriteClock(pAST, 0x00);				/* Set Clk Low */
    I2CDelay(pAST);    
    I2CWriteData(pAST, 0x00);				/* Set Data Low */
    I2CDelay(pAST);    
    I2CWriteClock(pAST, 0x01);				/* Set Clk High */
    I2CDelay(pAST);    
    I2CWriteData(pAST, 0x01);				/* Set Data High */
    I2CDelay(pAST);    
    I2CWriteClock(pAST, 0x01);				/* Set Clk High */
    I2CDelay(pAST);                      
	
}

Bool 
CheckACK(ASTRecPtr pAST)
{
    UCHAR Data;

    I2CWriteClock(pAST, 0x00);				/* Set Clk Low */
    I2CDelay(pAST);    
    I2CWriteData(pAST, 0x01);				/* Set Data High */
    I2CDelay(pAST);    
    I2CWriteClock(pAST, 0x01);				/* Set Clk High */
    I2CDelay(pAST);    
    Data = (UCHAR) I2CReadData(pAST);			/* Set Data High */
    
    return ((Data & 0x01) ? 0:1);                
                
}


void 
SendACK(ASTRecPtr pAST)
{

    I2CWriteClock(pAST, 0x00);				/* Set Clk Low */
    I2CDelay(pAST);    
    I2CWriteData(pAST, 0x00);				/* Set Data low */
    I2CDelay(pAST);    
    I2CWriteClock(pAST, 0x01);				/* Set Clk High */
    I2CDelay(pAST);    
                	
}

void 
SendNACK(ASTRecPtr pAST)
{

    I2CWriteClock(pAST, 0x00);				/* Set Clk Low */
    I2CDelay(pAST);    
    I2CWriteData(pAST, 0x01);				/* Set Data high */
    I2CDelay(pAST);    
    I2CWriteClock(pAST, 0x01);				/* Set Clk High */
    I2CDelay(pAST);    
                		
}

void 
SendI2CDataByte(ASTRecPtr pAST, UCHAR data)
{
    UCHAR jData;
    LONG i;

    for (i=7;i>=0;i--)
    {
        I2CWriteClock(pAST, 0x00);				/* Set Clk Low */
        I2CDelay(pAST);     	
        
    	jData = ((data >> i) & 0x01) ? 1:0;
        I2CWriteData(pAST, jData);				/* Set Data Low */
        I2CDelay(pAST);     	
        
        I2CWriteClock(pAST, 0x01);				/* Set Clk High */
        I2CDelay(pAST);                       	
    }                
}

UCHAR 
ReceiveI2CDataByte(ASTRecPtr pAST)
{
    UCHAR jData=0, jTempData;   
    LONG i, j;

    for (i=7;i>=0;i--)
    {
        I2CWriteClock(pAST, 0x00);				/* Set Clk Low */
        I2CDelay(pAST);     
        	
        I2CWriteData(pAST, 0x01);				/* Set Data High */
        I2CDelay(pAST);     	
        
        I2CWriteClock(pAST, 0x01);				/* Set Clk High */
        I2CDelay(pAST);           
        
        for (j=0; j<0x1000; j++)
        {   
            if (I2CReadClock(pAST)) break;
        }    
        	        
    	jTempData =  I2CReadData(pAST);
    	jData |= ((jTempData & 0x01) << i); 

        I2CWriteClock(pAST, 0x0);				/* Set Clk Low */
        I2CDelay(pAST);                       	
    }    
    
    return ((UCHAR)jData);                              
}        

Bool
GetVGAEDID(ScrnInfoPtr pScrn, unsigned char *pEDIDBuffer)
{
    ASTRecPtr pAST;	
    UCHAR *pjDstEDID;
    UCHAR jData;
    ULONG i;

    pAST = ASTPTR(pScrn);    
    pjDstEDID = (UCHAR *) pEDIDBuffer;
    
    /* Force to DDC2 */
    I2CWriteClock(pAST, 0x01);				/* Set Clk Low */
    I2CDelay(pAST);  
    I2CDelay(pAST);     	       	
    I2CWriteClock(pAST, 0x00);				/* Set Clk Low */
    I2CDelay(pAST);     	
                    
    I2CStart(pAST);
    
    SendI2CDataByte(pAST, 0xA0);
    if (!CheckACK(pAST))
    {
	 xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[GetVGAEDID] Check ACK Failed \n");
    	 return (FALSE);
    }	
    
    SendI2CDataByte(pAST, 0x00);
    if (!CheckACK(pAST))
    {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[GetVGAEDID] Check ACK Failed \n");
    	return (FALSE);
    }	
    
    I2CStart(pAST);
    
    SendI2CDataByte(pAST, 0xA1);
    if (!CheckACK(pAST))
    {
	xf86DrvMsg(pScrn->scrnIndex, X_INFO, "[GetVGAEDID] Check ACK Failed \n");
    	return (FALSE);
    }	
            
    for (i=0; i<127; i++)
    {
        jData = ReceiveI2CDataByte(pAST);
        SendACK(pAST);
        
        *pjDstEDID++ = jData;       
    }
    
    jData = ReceiveI2CDataByte(pAST);
    SendNACK(pAST);
    *pjDstEDID = jData;       
            
    I2CStop(pAST);
      
    return (TRUE);
    
} /* GetVGAEDID */

Bool bInitAST1180(ScrnInfoPtr pScrn)
{
    ASTRecPtr pAST;	
    ULONG ulData;

    pAST = ASTPTR(pScrn);    

    /* Enable PCI */
    PCI_READ_LONG(pAST->PciInfo, &ulData, 0x04);
    ulData |= 0x03;
    PCI_WRITE_LONG(pAST->PciInfo, ulData, 0x04);       
    	
    /* init DRAM if no F/W */
    /* TODO */

    WriteAST1180SOC(AST1180_MMC_BASE+0x00, 0xFC600309);	/* unlock */
    WriteAST1180SOC(AST1180_SCU_BASE+0x00, 0x1688a8a8);	/* unlock */
    usleep(100);
    
    WriteAST1180SOC(AST1180_MMC_BASE+0x08, 0x000011e3);	/* req. */    
    	
    /* init SCU */
#if 0
    ReadAST1180SOC(AST1180_SCU_BASE+0x08, ulData);	/* delay compensation */
    ulData &= 0xFFFFE0FF;     
    ulData |= 0x00000C00;
    WriteAST1180SOC(AST1180_SCU_BASE+0x08, ulData);
#endif

    ReadAST1180SOC(AST1180_SCU_BASE+0x0c, ulData);	/* 2d clk */
    ulData &= 0xFFFFFFFD;
    WriteAST1180SOC(AST1180_SCU_BASE+0x0c, ulData);
	
	
} /* bInitAST1180 */
	
void GetAST1180DRAMInfo(ScrnInfoPtr pScrn)
{
    ASTRecPtr pAST = ASTPTR(pScrn);
    ULONG ulData;
    
    WriteAST1180SOC(AST1180_MMC_BASE+0x00, 0xFC600309);	/* unlock */
    ReadAST1180SOC(AST1180_MMC_BASE+0x04, ulData);
    pAST->ulDRAMBusWidth = 32;
    if (ulData & 0x40)
        pAST->ulDRAMBusWidth = 16;
        
    /* DRAM size */    
    switch (ulData & 0x0C)
    {
    case 0x00:
       pAST->ulDRAMSize = DRAM_SIZE_032M;
       break;    
    case 0x04:
       pAST->ulDRAMSize = DRAM_SIZE_064M;
       break;        
    case 0x08:
       pAST->ulDRAMSize = DRAM_SIZE_128M;
       break;        
    case 0x0c:	
       pAST->ulDRAMSize = DRAM_SIZE_256M;
       break;        
    }	

    /* Get framebuffer size */
    switch (ulData & 0x30)
    {
    case 0x00:
        pAST->ulVRAMSize = DRAM_SIZE_016M;
        break;
    case 0x10:
        pAST->ulVRAMSize = DRAM_SIZE_032M;
        break;    
    case 0x20:
        pAST->ulVRAMSize = DRAM_SIZE_064M;
        break;    
    case 0x30:
        pAST->ulVRAMSize = DRAM_SIZE_128M;
        break;    	
    }	

    /* VRAM base */
    if (pAST->ulVRAMSize >= pAST->ulDRAMSize)
        pAST->ulVRAMSize = pAST->ulDRAMSize;    
    pAST->ulVRAMBase = pAST->ulDRAMSize - pAST->ulVRAMSize;
    
    /* MCLK */
    pAST->ulMCLK = 200;
    
} /* GetAST1180DRAMInfo */
