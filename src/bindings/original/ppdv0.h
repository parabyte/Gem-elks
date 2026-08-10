extern WORD contrl[], intin[], ptsin[], intout[], ptsout[];

extern GEM_BINDINGS_VDIPB pblock;

#define i_ptsin(ptr) gem_bindings_vdi_set_slot(&pblock.ptsin, (ptr))
#define i_intin(ptr) gem_bindings_vdi_set_slot(&pblock.intin, (ptr))
#define i_intout(ptr) gem_bindings_vdi_set_slot(&pblock.intout, (ptr))
#define i_ptsout(ptr) gem_bindings_vdi_set_slot(&pblock.ptsout, (ptr))

/*
 * VDI places two far MFDB pointers in contrl words 7..10.  Treat those four
 * words as two packed offset/segment slots rather than C pointer lvalues.
 */
#define i_ptr(ptr) \
	gem_bindings_store_pointer((GEM_BINDINGS_POINTER_SLOT *) &contrl[7], \
				   (ptr))
#define i_lptr1(ptr) i_ptr(ptr)
#define i_ptr2(ptr) \
	gem_bindings_store_pointer((GEM_BINDINGS_POINTER_SLOT *) &contrl[9], \
				   (ptr))
#define m_lptr2(ptr) \
	*(ptr) = gem_bindings_load_pointer( \
		(const GEM_BINDINGS_POINTER_SLOT *) &contrl[9])
