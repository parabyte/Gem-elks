

#include "ppdgem.h"
#include "ppdv0.h"


    WORD
v_pline( handle, count, xy )
WORD handle, count, xy[];
{
    WORD result;

    i_ptsin( xy );

    contrl[0] = 6;
    contrl[1] = count;
    contrl[3] = 0;
    contrl[6] = handle;
    result = vdi();

    i_ptsin( ptsin );
    return result;
}
