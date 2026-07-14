/*
*	-------------------------------------------------------------
*	GEM Desktop					  Version 2.3
*	Serial No.  XXXX-0000-654321		  All Rights Reserved
*	Copyright (C) 1985 - 1987		Digital Research Inc.
*	-------------------------------------------------------------
*/

#define CTRL_CNT	3		/* table stores intin, intout, addrin */

extern BYTE		ctrl_cnts[];
extern GEM_BINDINGS_AESPB gb;
extern UWORD		control[C_SIZE];
extern WORD		global[G_SIZE];
extern UWORD		int_in[I_SIZE];
extern UWORD		int_out[O_SIZE];
extern GEM_BINDINGS_POINTER_SLOT addr_in[AI_SIZE];
extern GEM_BINDINGS_POINTER_SLOT addr_out[AO_SIZE];

extern GEM_BINDINGS_AESPB *ad_g;

extern WORD gem_if(WORD opcode);
