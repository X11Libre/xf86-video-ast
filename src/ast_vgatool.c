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
#include "xf86Resources.h"
#include "xf86RAC.h"
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
ULONG GetVRAMInfo(ScrnInfoPtr pScrn);
ULONG GetMaxDCLK(ScrnInfoPtr pScrn);
void GetChipType(ScrnInfoPtr pScrn);
void vAST1000DisplayOn(ASTRecPtr pAST);
void vAST1000DisplayOff(ASTRecPtr pAST);
void vSetStartAddressCRT1(ASTRecPtr pAST, ULONG base);
void vASTLoadPalette(ScrnInfoPtr pScrn, int numColors, int *indices, LOCO *colors, VisualPtr pVisual);
void ASTDisplayPowerManagementSet(ScrnInfoPtr pScrn, int PowerManagementMode, int flags);
Bool GetVGA2EDID(ScrnInfoPtr pScrn, unsigned char *pEDIDBuffer);

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
   ULONG ulData, ulData2;
   ULONG ulRefPLL, ulDeNumerator, ulNumerator, ulDivider;
   ULONG ulDRAMBusWidth, ulMCLK, ulDRAMBandwidth, ActualDRAMBandwidth, DRAMEfficiency = 500;
   ULONG ulDCLK;
	
   vASTOpenKey(pScrn);

   *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
   *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x00000001;

   *(ULONG *) (pAST->MMIOVirtualAddr + 0x10100) = 0x000000A8;

   do {
      ;	
   } while (*(volatile ULONG *) (pAST->MMIOVirtualAddr + 0x10100) != 0x000000A8);
   	   
   /* Get BusWidth */
   ulData = *(ULONG * ) (pAST->MMIOVirtualAddr + 0x10004);       
   if (ulData & 0x40)
      ulDRAMBusWidth = 16;
   else    
      ulDRAMBusWidth = 32;
          
   /* Get MCLK */
   {   	
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
       
       ulMCLK = ulRefPLL * (ulNumerator + 2) / ((ulDeNumerator + 2) * ulDivider * 1000);          
       
   }  
   
   /* Get Bandwidth */
   /* Modify DARM utilization to 60% for AST1100/2100 16bits DRAM, ycchen@032508 */
   if ( ((pAST->jChipType == AST2100) || (pAST->jChipType == AST1100)) && (ulDRAMBusWidth == 16) )
       DRAMEfficiency = 600;     
   ulDRAMBandwidth = ulMCLK * ulDRAMBusWidth * 2 / 8;
   ActualDRAMBandwidth = ulDRAMBandwidth * DRAMEfficiency / 1000;
   
   /* Get Max DCLK */    
   GetIndexRegMask(CRTC_PORT, 0xD0, 0xFF, jReg);  
   if (jReg & 0x08)                      
       ulDCLK = ActualDRAMBandwidth / ((pScrn->bitsPerPixel+1+16) / 8);	
   else    
       ulDCLK = ActualDRAMBandwidth / ((pScrn->bitsPerPixel+1) / 8);	   

   /* Add for AST2100, ycchen@061807 */
   if (pAST->jChipType == AST2100)
       if (ulDCLK > 200) ulDCLK = 200;
   else
       if (ulDCLK > 165) ulDCLK = 165;       
    
   return(ulDCLK);
   
}

void
GetChipType(ScrnInfoPtr pScrn)
{
   ASTRecPtr pAST = ASTPTR(pScrn);
   ULONG ulData;
   
   pAST->jChipType = AST2100;

   *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
   *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;        

   ulData = *(ULONG *) (pAST->MMIOVirtualAddr + 0x1207c);       
   
   if ((ulData & 0x0300) == 0x0200)
       pAST->jChipType = AST1100;   
}

void
vSetStartAddressCRT1(ASTRecPtr pAST, ULONG base)
{
    SetIndexReg(CRTC_PORT,0x0D, (UCHAR) (base & 0xFF));
    SetIndexReg(CRTC_PORT,0x0C, (UCHAR) ((base >> 8) & 0xFF));
    SetIndexReg(CRTC_PORT,0xAF, (UCHAR) ((base >> 16) & 0xFF));
    	
}

void
vAST1000DisplayOff(ASTRecPtr pAST)
{
    SetIndexRegMask(SEQ_PORT,0x01, 0xDF, 0x20);
	    
}


void
vAST1000DisplayOn(ASTRecPtr pAST)
{
	
    SetIndexRegMask(SEQ_PORT,0x01, 0xDF, 0x00);	
    
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

   pAST = ASTPTR(pScrn);
   SEQ01=CRB6=0;
   
   vASTOpenKey(pScrn);
   
   switch (PowerManagementMode) {
   case DPMSModeOn:
      /* Screen: On; HSync: On, VSync: On */
      SEQ01 = 0x00;
      CRB6 = 0x00;
      break;
   case DPMSModeStandby:
      /* Screen: Off; HSync: Off, VSync: On */
      SEQ01 = 0x20;
      CRB6  = 0x01;     
      break;
   case DPMSModeSuspend:
      /* Screen: Off; HSync: On, VSync: Off */
      SEQ01 = 0x20;
      CRB6  = 0x02;           
      break;
   case DPMSModeOff:
      /* Screen: Off; HSync: Off, VSync: Off */
      SEQ01 = 0x20;
      CRB6  = 0x03;           
      break;
   }

   SetIndexRegMask(SEQ_PORT,0x01, 0xDF, SEQ01);
   SetIndexRegMask(CRTC_PORT,0xB6, 0xFC, CRB6);


}

#define I2C_BASE	0x1e780000
#define I2C_OFFSET	(0xA000 + 0x40 * 4)	/* port4 */
#define I2C_DEVICEADDR	0x0A0			/* slave addr */

Bool
GetVGA2EDID(ScrnInfoPtr pScrn, unsigned char *pEDIDBuffer)
{
    ASTRecPtr pAST = ASTPTR(pScrn);
    UCHAR *ulI2CBase = pAST->MMIOVirtualAddr + 0x10000 + I2C_OFFSET;
    ULONG i, ulData;    
    UCHAR *pjEDID;
    
    pjEDID = pEDIDBuffer;

    /* SCU settings */
    *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = 0x1e6e0000;
    *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;
    xf86UDelay(10000);

    *(ULONG *) (pAST->MMIOVirtualAddr + 0x12000) = 0x1688A8A8;    
    ulData = *(ULONG *) (pAST->MMIOVirtualAddr + 0x12004);
    ulData &= 0xfffffffb;
    *(ULONG *) (pAST->MMIOVirtualAddr + 0x12004) = ulData;    
    xf86UDelay(10000);
    
    /* I2C settings */
    *(ULONG *) (pAST->MMIOVirtualAddr + 0xF004) = I2C_BASE;
    *(ULONG *) (pAST->MMIOVirtualAddr + 0xF000) = 0x1;
    xf86UDelay(10000);
    
    /* I2C Start */
    *(ULONG *) (ulI2CBase + 0x00) = 0x0;
    *(ULONG *) (ulI2CBase + 0x04) = 0x77777355;
    *(ULONG *) (ulI2CBase + 0x08) = 0x0;
    *(ULONG *) (ulI2CBase + 0x10) = 0xffffffff;
    *(ULONG *) (ulI2CBase + 0x00) = 0x1;
    *(ULONG *) (ulI2CBase + 0x0C) = 0xAF;
    *(ULONG *) (ulI2CBase + 0x20) = I2C_DEVICEADDR;
    *(ULONG *) (ulI2CBase + 0x14) = 0x03;
    do {
        ulData = *(volatile ULONG *) (ulI2CBase + 0x10);
    } while (!(ulData & 0x03));
    if (ulData & 0x02)				/* NACK */
        return (FALSE);
    *(ULONG *) (ulI2CBase + 0x10) = 0xffffffff;
    *(ULONG *) (ulI2CBase + 0x20) = (ULONG) 0;	/* Offset */
    *(ULONG *) (ulI2CBase + 0x14) = 0x02;
    do {
        ulData = *(volatile ULONG *) (ulI2CBase + 0x10);
    } while (!(ulData & 0x01));
    *(ULONG *) (ulI2CBase + 0x10) = 0xffffffff;
    *(ULONG *) (ulI2CBase + 0x20) = I2C_DEVICEADDR + 1;
    *(ULONG *) (ulI2CBase + 0x14) = 0x03; 
    do {
        ulData = *(volatile ULONG *) (ulI2CBase + 0x10);
    } while (!(ulData & 0x01));
    
    /* I2C Read */
    for (i=0; i<127; i++)
    {
        *(ULONG *) (ulI2CBase + 0x10) = 0xffffffff;
        *(ULONG *) (ulI2CBase + 0x0C) |= 0x10;
        *(ULONG *) (ulI2CBase + 0x14) = 0x08;
        do {
            ulData = *(volatile ULONG *) (ulI2CBase + 0x10);
        } while (!(ulData & 0x04));
        *(ULONG *) (ulI2CBase + 0x10) = 0xffffffff;
        *(UCHAR *) (pjEDID++) = (UCHAR) ((*(ULONG *) (ulI2CBase + 0x20) & 0xFF00) >> 8);        	
    }

    /* Read Last Byte */
    *(ULONG *) (ulI2CBase + 0x10) = 0xffffffff;
    *(ULONG *) (ulI2CBase + 0x0C) |= 0x10;
    *(ULONG *) (ulI2CBase + 0x14) = 0x18;
    do {
        ulData = *(volatile ULONG *) (ulI2CBase + 0x10);
    } while (!(ulData & 0x04));
    *(ULONG *) (ulI2CBase + 0x10) = 0xffffffff;
    *(UCHAR *) (pjEDID++) = (UCHAR) ((*(ULONG *) (ulI2CBase + 0x20) & 0xFF00) >> 8);        	

    /* I2C Stop	 */
    *(ULONG *) (ulI2CBase + 0x10) = 0xffffffff;
    *(ULONG *) (ulI2CBase + 0x14) = 0x20;
    do {
        ulData = *(volatile ULONG *) (ulI2CBase + 0x10);
    } while (!(ulData & 0x10));
    *(ULONG *) (ulI2CBase + 0x0C) &= 0xffffffef;        
    *(ULONG *) (ulI2CBase + 0x10) = 0xffffffff;
    
    return (TRUE);

} /* GetVGA2EDID */
