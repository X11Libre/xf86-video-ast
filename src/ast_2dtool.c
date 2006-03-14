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

#include "xf86.h"
#include "xf86_ansic.h"
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
#include "Xv.h"
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

#ifdef	Accel_2D

/* Prototype type declaration */
Bool bInitCMDQInfo(ScrnInfoPtr pScrn, ASTRecPtr pAST);
Bool bEnableCMDQ(ScrnInfoPtr pScrn, ASTRecPtr pAST);
Bool bEnable2D(ScrnInfoPtr pScrn, ASTRecPtr pAST);
void vDisable2D(ScrnInfoPtr pScrn, ASTRecPtr pAST);
void vWaitEngIdle(ScrnInfoPtr pScrn, ASTRecPtr pAST);    
UCHAR *pjRequestCMDQ(ASTRecPtr pAST, ULONG   ulDataLen);
Bool bGetLineTerm(_LINEInfo *LineInfo, LINEPARAM *dsLineParam);
LONG lGetDiaRg(LONG GFracX, LONG GFracY);

Bool
bInitCMDQInfo(ScrnInfoPtr pScrn, ASTRecPtr pAST)
{

    pAST->CMDQInfo.pjCmdQBasePort    = pAST->MMIOVirtualAddr+ 0x8044; 
    pAST->CMDQInfo.pjWritePort       = pAST->MMIOVirtualAddr+ 0x8048;
    pAST->CMDQInfo.pjReadPort        = pAST->MMIOVirtualAddr+ 0x804C;
    pAST->CMDQInfo.pjEngStatePort    = pAST->MMIOVirtualAddr+ 0x804C;

    /* CMDQ mode Init */
    if (!pAST->MMIO2D) {
        pAST->CMDQInfo.ulCMDQType = VM_CMD_QUEUE;	

        ScreenPtr pScreen;
       
        pScreen = screenInfo.screens[pScrn->scrnIndex];
      
        pAST->pCMDQPtr = xf86AllocateOffscreenLinear (pScreen, 1024*1024, 8, NULL, NULL, NULL);
      
        if (!pAST->pCMDQPtr) {   		  
           xf86DrvMsg(pScrn->scrnIndex, X_ERROR,"Allocate CMDQ failed \n");
           pAST->MMIO2D = TRUE;		/* set to MMIO mode if CMDQ allocate failed */
        }	
        						  
        pAST->CMDQInfo.ulCMDQOffsetAddr  = pAST->pCMDQPtr->offset*((pScrn->bitsPerPixel + 1) / 8);
        pAST->CMDQInfo.pjCMDQVirtualAddr = pAST->FBVirtualAddr + pAST->CMDQInfo.ulCMDQOffsetAddr;
           						 
        pAST->CMDQInfo.ulCurCMDQueueLen = pAST->CMDQInfo.ulCMDQSize - CMD_QUEUE_GUARD_BAND;
        pAST->CMDQInfo.ulCMDQMask = pAST->CMDQInfo.ulCMDQSize - 1 ; 
    }
    
    /* MMIO mode init */  
    if (pAST->MMIO2D) {    	
        pAST->CMDQInfo.ulCMDQType = VM_CMD_MMIO;    	
    }
       
    return (TRUE);	
}

Bool
bEnableCMDQ(ScrnInfoPtr pScrn, ASTRecPtr pAST)
{
    ULONG ulVMCmdQBasePort = 0;

    vWaitEngIdle(pScrn, pAST);  

    /* set DBG Select Info */
    if (pAST->DBGSelect)
    {
        *(ULONG *) (pAST->MMIOVirtualAddr + 0x804C) = (ULONG) (pAST->DBGSelect);             	
    }
    
    /* set CMDQ base */
    switch (pAST->CMDQInfo.ulCMDQType)
    {
    case VM_CMD_QUEUE:
        ulVMCmdQBasePort  = (pAST->CMDQInfo.ulCMDQOffsetAddr - 0) >> 3;
 
        /* set CMDQ Threshold */
        ulVMCmdQBasePort |= 0xF0000000;			   

        /* set CMDQ Size */
        switch (pAST->CMDQInfo.ulCMDQSize)
        {
        case CMD_QUEUE_SIZE_256K:
            ulVMCmdQBasePort |= 0x00000000;   
            break;
        	
        case CMD_QUEUE_SIZE_512K:
            ulVMCmdQBasePort |= 0x04000000;   
            break;
      
        case CMD_QUEUE_SIZE_1M:
            ulVMCmdQBasePort |= 0x08000000;       
            break;
            
        case CMD_QUEUE_SIZE_2M:
            ulVMCmdQBasePort |= 0x0C000000;       
            break;        
            
        default:
            return(FALSE);
            break;
        }     
                                 
        *(ULONG *) (pAST->CMDQInfo.pjCmdQBasePort) = ulVMCmdQBasePort;         
        pAST->CMDQInfo.ulWritePointer = *(ULONG *) (pAST->CMDQInfo.pjWritePort);                 
        break;
        
    case VM_CMD_MMIO:
        /* set CMDQ Threshold */
        ulVMCmdQBasePort |= 0xF0000000;			   
    
        ulVMCmdQBasePort |= 0x02000000;			/* MMIO mode */
        *(ULONG *) (pAST->CMDQInfo.pjCmdQBasePort) = ulVMCmdQBasePort;                 		       
        break;
        
    default:
        return (FALSE);
        break;
    }

    return (TRUE);	
}

Bool
bEnable2D(ScrnInfoPtr pScrn, ASTRecPtr pAST)
{
    SetIndexRegMask(CRTC_PORT, 0xA4, 0xFE, 0x01);		/* enable 2D */  
   
    if (!bInitCMDQInfo(pScrn, pAST))
    {
        vDisable2D(pScrn, pAST);  	
    	return (FALSE);
    }
        
    if (!bEnableCMDQ(pScrn, pAST))
    {
        vDisable2D(pScrn, pAST);  	
    	return (FALSE);
    }
            
    return (TRUE);	
}

void
vDisable2D(ScrnInfoPtr pScrn, ASTRecPtr pAST)
{
	
    vWaitEngIdle(pScrn, pAST);
    vWaitEngIdle(pScrn, pAST);

    SetIndexRegMask(CRTC_PORT, 0xA4, 0xFE, 0x00);		  
	
}


void
vWaitEngIdle(ScrnInfoPtr pScrn, ASTRecPtr pAST)
{
    ULONG ulEngState, ulEngState2;
    UCHAR jReg;
    ULONG ulEngCheckSetting; 
    
    if (pAST->MMIO2D)     
        ulEngCheckSetting = 0x10000000;
    else
        ulEngCheckSetting = 0x80000000;
    
    /* 2D disable if 0xA4 D[0] = 1 */
    GetIndexRegMask(CRTC_PORT, 0xA4, 0x01, jReg);  
    if (!jReg) goto Exit_vWaitEngIdle;
    
    /* 2D not work if in std. mode */
    GetIndexRegMask(CRTC_PORT, 0xA3, 0x0F, jReg);  
    if (!jReg) goto Exit_vWaitEngIdle;

    do  
    {
        ulEngState = (*(volatile ULONG *)(pAST->CMDQInfo.pjEngStatePort)) & 0xFFFC0000;
        ulEngState2 = (*(volatile ULONG *)(pAST->CMDQInfo.pjEngStatePort)) & 0xFFFC0000;
        ulEngState2 = (*(volatile ULONG *)(pAST->CMDQInfo.pjEngStatePort)) & 0xFFFC0000;
        ulEngState2 = (*(volatile ULONG *)(pAST->CMDQInfo.pjEngStatePort)) & 0xFFFC0000;
        ulEngState2 = (*(volatile ULONG *)(pAST->CMDQInfo.pjEngStatePort)) & 0xFFFC0000;
        ulEngState2 = (*(volatile ULONG *)(pAST->CMDQInfo.pjEngStatePort)) & 0xFFFC0000;
                      
    } while ((ulEngState & ulEngCheckSetting) || (ulEngState != ulEngState2));
    
Exit_vWaitEngIdle:
    ;   	
}    

/* ULONG ulGetCMDQLength() */
__inline ULONG ulGetCMDQLength(ASTRecPtr pAST, ULONG ulWritePointer, ULONG ulCMDQMask)
{
    ULONG ulReadPointer, ulReadPointer2;
    
    do {
        ulReadPointer  = *((volatile ULONG *)(pAST->CMDQInfo.pjReadPort)) & 0x0003FFFF;    	
        ulReadPointer2 = *((volatile ULONG *)(pAST->CMDQInfo.pjReadPort)) & 0x0003FFFF;
        ulReadPointer2 = *((volatile ULONG *)(pAST->CMDQInfo.pjReadPort)) & 0x0003FFFF;
        ulReadPointer2 = *((volatile ULONG *)(pAST->CMDQInfo.pjReadPort)) & 0x0003FFFF;
        ulReadPointer2 = *((volatile ULONG *)(pAST->CMDQInfo.pjReadPort)) & 0x0003FFFF;
        ulReadPointer2 = *((volatile ULONG *)(pAST->CMDQInfo.pjReadPort)) & 0x0003FFFF;        
     } while (ulReadPointer != ulReadPointer2);

    return ((ulReadPointer << 3) - ulWritePointer - CMD_QUEUE_GUARD_BAND) & ulCMDQMask;
}

UCHAR *pjRequestCMDQ(
ASTRecPtr pAST, ULONG   ulDataLen)
{
    UCHAR   *pjBuffer;
    ULONG   i, ulWritePointer, ulCMDQMask, ulCurCMDQLen, ulContinueCMDQLen;

    ulWritePointer = pAST->CMDQInfo.ulWritePointer;
    ulContinueCMDQLen = pAST->CMDQInfo.ulCMDQSize - ulWritePointer;
    ulCMDQMask = pAST->CMDQInfo.ulCMDQMask;        
    
    if (ulContinueCMDQLen >= ulDataLen)
    {
        /* Get CMDQ Buffer */            	
        if (pAST->CMDQInfo.ulCurCMDQueueLen >= ulDataLen)
        {
        	;
        }
        else
        {
           
            do
            {
                ulCurCMDQLen = ulGetCMDQLength(pAST, ulWritePointer, ulCMDQMask);
            } while (ulCurCMDQLen < ulDataLen);
            
            pAST->CMDQInfo.ulCurCMDQueueLen = ulCurCMDQLen;

        }
        
        pjBuffer = pAST->CMDQInfo.pjCMDQVirtualAddr + ulWritePointer;
        pAST->CMDQInfo.ulCurCMDQueueLen -= ulDataLen;            
        pAST->CMDQInfo.ulWritePointer = (ulWritePointer + ulDataLen) & ulCMDQMask;
        return pjBuffer;            
    }
    else
    {   

        /* Fill NULL CMD to the last of the CMDQ */
        if (pAST->CMDQInfo.ulCurCMDQueueLen >= ulContinueCMDQLen)
        {
        	;
        }
        else
        {
           
            do
            {
                ulCurCMDQLen = ulGetCMDQLength(pAST, ulWritePointer, ulCMDQMask);
            } while (ulCurCMDQLen < ulContinueCMDQLen);
            
            pAST->CMDQInfo.ulCurCMDQueueLen = ulCurCMDQLen;

        }
    
        pjBuffer = pAST->CMDQInfo.pjCMDQVirtualAddr + ulWritePointer;
        for (i = 0; i<ulContinueCMDQLen/8; i++, pjBuffer+=8)
        {
            *(ULONG *)pjBuffer = (ULONG) PKT_NULL_CMD;
            *(ULONG *) (pjBuffer+4) = 0;
            
        }
        pAST->CMDQInfo.ulCurCMDQueueLen -= ulContinueCMDQLen;
        pAST->CMDQInfo.ulWritePointer = ulWritePointer = 0;
            
        /* Get CMDQ Buffer */    
        if (pAST->CMDQInfo.ulCurCMDQueueLen >= ulDataLen)
        {
	        ;	
        }
        else
        {
           
            do
            {
                ulCurCMDQLen = ulGetCMDQLength(pAST, ulWritePointer, ulCMDQMask);
            } while (ulCurCMDQLen < ulDataLen);
            
            pAST->CMDQInfo.ulCurCMDQueueLen = ulCurCMDQLen;

        }
        
        pAST->CMDQInfo.ulCurCMDQueueLen -= ulDataLen;
        pjBuffer = pAST->CMDQInfo.pjCMDQVirtualAddr + ulWritePointer;
        pAST->CMDQInfo.ulWritePointer = (ulWritePointer + ulDataLen) & ulCMDQMask;
        return pjBuffer;            
        
    }
   
} /* end of pjRequestCmdQ() */

Bool bGetLineTerm(_LINEInfo *LineInfo, LINEPARAM *dsLineParam)
{
    LONG GAbsX, GAbsY, GXInc, GYInc, GSlopeOne, GXMajor;
    Bool tmpFlipH=0, tmpFlipV=0, tmpFlipD=0;
    LONG tmpStartX, tmpStartY, tmpFStartX, tmpFStartY;
    LONG GFAbsX, GFAbsY, GFStartX, GFStartY, GFFracX[2], GFFracY[2];  
    LONG flag, GXRoundDown, GYRoundDown, GFlipH, GFlipV, GFlipD;
    LONG tmpx, tmpy, GFGamma;
    LONG i, region, tmpDiaRg[2], tmpGFX1, GFAdd, GNTWidth;           	
    LONG tmp1GFX, tmp1GFY, tmp2GFX, tmp2GFY, tmpGK1Term, tmpGK2Term, tmpGNTErr;
    LONG GFX, GFY, GK1Term, GK2Term, GNTErr;
            	
    /*Init Calucate */
#ifdef LONG64    
    GAbsX = abs (LineInfo->X1 - LineInfo->X2);
    GAbsY = abs (LineInfo->Y1 - LineInfo->Y2);
#else
    GAbsX = labs (LineInfo->X1 - LineInfo->X2);
    GAbsY = labs (LineInfo->Y1 - LineInfo->Y2);
#endif    

    GXInc = (LineInfo->X1 < LineInfo->X2) ? 1:0;
    GYInc = (LineInfo->Y1 < LineInfo->Y2) ? 1:0;
    GSlopeOne = (GAbsX ==GAbsY) ? 1:0;
    GXMajor = (GAbsX >= GAbsY) ? 1:0;
   
    /*Flip */
    tmpStartX = LineInfo->X1;
    tmpStartY = LineInfo->Y1;
    if (!GXInc)
    {
        tmpStartX = ~LineInfo->X1+ 1;
        tmpFlipH = 1;	
    }
    
    if (!GYInc)
    {
        tmpStartY = ~LineInfo->Y1 + 1;
        tmpFlipV = 1;	
    }
    
    if (GXMajor ==0)
    {
    	tmpFlipD = 1;
    	tmpFStartX = tmpStartY;
    	tmpFStartY = tmpStartX;
    	GFAbsX = GAbsY;
    	GFAbsY = GAbsX;
    }
    else
    {
    	tmpFlipD = 0;
    	tmpFStartX = tmpStartX;
    	tmpFStartY = tmpStartY;
    	GFAbsX = GAbsX;
    	GFAbsY = GAbsY;    	
    }
        
    GFStartX = tmpFStartX >> 4;
    GFStartY = tmpFStartY >> 4;
    GFFracX[0] = tmpFStartX & 0xF;
    GFFracY[0] = tmpFStartY & 0xF;
  
    /* Flag = GSlopeOne & tmpFlipH & tmpFlipV & tmpFlipD */
    flag = (GSlopeOne<<3) + (tmpFlipH<<2) + (tmpFlipV<<1) + tmpFlipD;
    switch(flag)			/*  GSlopeOne tmpFlipH tmpFlipV tmpFlipD */
    {
    case 0:				/*  0           0        0        0 */
    case 1: 				/*  0           0        0        1 */
        GXRoundDown = 1;	        
 	GYRoundDown = 1;
	GFlipH = tmpFlipH;
	GFlipV = tmpFlipV;
	GFlipD = tmpFlipD;
	break;
    case 2:				/*  0           0        1        0 */
    case 5:				/*  0           1        0        1 */
    case 10:			        /*  1           0        1        0 */
    case 14:				/*  1           1        1        0 */
         GXRoundDown = 1;	        
 	 GYRoundDown = 0;
	 GFlipH = tmpFlipH;
	 GFlipV = tmpFlipV;
	 GFlipD = tmpFlipD;
	 break;
    case 3:				/*  0           0        1        1 */
    case 4:				/*  0           1        0        0 */
    case 8:				/*  1           0        0        0 */
    case 12:                            /*  1           1        0        0 */
        GXRoundDown = 0;	        
	GYRoundDown = 1;
	GFlipH = tmpFlipH;
	GFlipV = tmpFlipV;
	GFlipD = tmpFlipD;
	break;
    case 6:				/*  0           1        1        0 */
    case 7:				/*  0           1        1        1 */
        GXRoundDown = 0;	        
	GYRoundDown = 0;
	GFlipH = tmpFlipH;
	GFlipV = tmpFlipV;
	GFlipD = tmpFlipD;
	break;
    /* case 9, 11, 13, 15 */
    default:
        GXRoundDown = 1;		/*  1           0        0        1 */
	GYRoundDown = 1;		/*  1           0        1        1 */
	GFlipH = 1;			/*  1           1        0        1 */
	GFlipV = 1;			/*  1           1        1        1 */
	GFlipD = 1;
    }
	      
    /*Err */
    tmpx = (GFFracY[0] +8) * GFAbsX;
    tmpy =  GFFracX[0]    * GFAbsY;
    if(GYRoundDown==1)
	GFGamma=(signed)(tmpx - tmpy - 1) >> 4;
    else
	GFGamma=(signed)(tmpx - tmpy) >> 4;
    
    /*GIQ */
    GFFracX[1] = (GFFracX[0] + GFAbsX) & 0xF;
    GFFracY[1] = (GFFracY[0] + GFAbsY) & 0xF;

    for (i=0; i<2; i++)
    {
    	tmpDiaRg[i] = 0;
        region = lGetDiaRg(GFFracX[i], GFFracY[i]);

        if(region==1 && GXRoundDown==0)
    	    tmpDiaRg[i] |= 1;
      
        if(region==2 && (GSlopeOne==0 || GXRoundDown==0))
    	    tmpDiaRg[i] |= 1;
    
        if(region==3)
    	    tmpDiaRg[i] |= 1;
 
    }
    
    tmpGFX1  =((signed)(GFFracX[0]+GFAbsX)>>4)-1+tmpDiaRg[1];   /* signed left shifter!! */
    GFAdd    = tmpDiaRg[0];
    GNTWidth = tmpGFX1 - tmpDiaRg[0] + 1;
   
    /* FXY	 */
    tmpGK1Term = GFAbsY;
    tmpGK2Term = GFAbsY - GFAbsX;

    if(GFAdd==1){
	tmpGNTErr = GFGamma - GFAbsX + GFAbsY;
    }else{
	tmpGNTErr = GFGamma - GFAbsX;
    }

    tmp1GFX = GFStartX + GFAdd;
    if((signed)tmpGNTErr >= 0){
	tmp1GFY = GFStartY+1;
	GNTErr = tmpGNTErr + tmpGK2Term;
    }else{
	tmp1GFY = GFStartY;
	GNTErr = tmpGNTErr + tmpGK1Term;
    }

    if(GFlipD == 1){
	tmp2GFX = tmp1GFY;
	tmp2GFY = tmp1GFX;
    }else{
	tmp2GFX = tmp1GFX;
	tmp2GFY = tmp1GFY;
    }

    if(GFlipV == 1){
	GFY = ~tmp2GFY+1;
    }else{
	GFY = tmp2GFY;
    }

    if(GFlipH == 1){
	GFX = ~tmp2GFX+1;
    }else{
	GFX = tmp2GFX;
    }

    GK1Term = tmpGK1Term;
    GK2Term = tmpGK2Term;    
    
    /*save the Param to dsLineParam */
    dsLineParam->dsLineX = (USHORT) GFX;
    dsLineParam->dsLineY = (USHORT) GFY;
    dsLineParam->dsLineWidth = (USHORT) GNTWidth;
    dsLineParam->dwErrorTerm = (ULONG) GNTErr;
    dsLineParam->dwK1Term = GK1Term;
    dsLineParam->dwK2Term = GK2Term;

    dsLineParam->dwLineAttributes = 0;
    if (GXMajor) dsLineParam->dwLineAttributes |= LINEPARAM_XM;
    if (!GXInc) dsLineParam->dwLineAttributes |= LINEPARAM_X_DEC;
    if (!GYInc) dsLineParam->dwLineAttributes |= LINEPARAM_Y_DEC;
    
    return(TRUE);
    
}

LONG lGetDiaRg(LONG GFracX, LONG GFracY)
{
    LONG region;
    
	switch(GFracY)
	{
	case 0x0: 
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		case 0x3:
		case 0x4:
		case 0x5:
		case 0x6:
		case 0x7:
		   region=0;
		   break;
		case 0x8:
		   region=1;
		   break;
		default:
		   region=3;
	    }
	    break;
	case 0x1:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		case 0x3:
		case 0x4:
		case 0x5:
		case 0x6:
		    region=0;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0x2:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		case 0x3:
		case 0x4:
		case 0x5:
		    region=0; break;
		default:
		    region=3;
	    }
	    break;
	case 0x3:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		case 0x3:
		case 0x4:
		    region=0;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0x4:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		case 0x3:
		    region=0;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0x5:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		    region=0; break;
		default:
		    region=3;
	    }
	    break;
	case 0x6:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		    region=0;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0x7:
	    switch(GFracX)
	    {
		case 0x0:
		    region=0;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0x8:
	    switch(GFracX)
	    {
		case 0x0:
		    region=0;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0x9: 
	    switch(GFracX)
	    {
		case 0x0:
		    region=0; 
		    break;
		case 0x1:
		    region=2;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0xa:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		    region=0;
		    break;
		case 0x2:
		    region=2;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0xb:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		    region=0;
		    break;
		case 0x3: 
		    region=2;
		    break;
		default:                                  
		    region=3;
	    }
	    break;
	case 0xc:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		case 0x3:
		    region=0;
		    break;
		case 0x4:
		    region=2;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0xd:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		case 0x3:
		case 0x4:
		    region=0;
		    break;
		case 0x5:
		    region=2;
		    break;
		default:
		    region=3;
	    }
	    break;
	case 0xe:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		case 0x3:
		case 0x4:
		case 0x5:
		    region=0;
		    break;
		case 0x6:
		    region=2;
		    break;
		default:
		    region=3;
	    }
	    break;
	default:
	    switch(GFracX)
	    {
		case 0x0:
		case 0x1:
		case 0x2:
		case 0x3:
		case 0x4:
		case 0x5:
		case 0x6:
		    region=0;
		    break;
		case 0x7:
		    region=2;
		    break;
		default: 
		    region=3;
	    }
	}

    return (region);	

}

#endif	/* end of Accel_2D */


