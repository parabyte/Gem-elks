

#include "ppdgem.h"
#include "ppdv0.h"


    WORD
vro_cpyfm( handle, wr_mode, xy, srcMFDB, desMFDB )
WORD handle, wr_mode, xy[];
LPMFDB srcMFDB, desMFDB;
{
    WORD result;

    intin[0] = wr_mode;
    i_ptr( srcMFDB );
    i_ptr2( desMFDB );
    i_ptsin( xy );
    
    contrl[0] = 109;
    contrl[1] = 4;
    contrl[3] = 1;
    contrl[6] = handle;
    result = vdi();

    i_ptsin( ptsin );
    return result;
}
