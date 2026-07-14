

#include "ppdgem.h"
#include "ppdg0.h"



	WORD
menu_icheck(tree, itemnum, checkit)
	LPTREE		tree;
	WORD		itemnum, checkit;
{
	gem_bindings_store_pointer(&MM_ITREE, tree);
	ITEM_NUM = itemnum;
	CHECK_IT = checkit;
	return( gem_if(MENU_ICHECK) );
}
