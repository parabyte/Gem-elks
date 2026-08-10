/*
 * Classic GEM VDI font bindings missing from the compact Pacific binding set.
 *
 * The original applications keep the historical five-array VDIPB.  These
 * wrappers only arrange byte-sized characters in 16-bit VDI words and then
 * use the same checked vdisys IPC path as every other drawing operation.
 * Counts, font identifiers, point sizes, and metrics remain one word.  The
 * sole fixed-point field is VST_ROTATION: one signed word stores tenths of a
 * degree (scale 10) and is passed unchanged, with no rounding in this wrapper.
 * No multiplication, division, allocation, or floating point is used.
 */

#include "ppdgem.h"
#include "ppdv0.h"

#define GEM_VDI_FONT_STRING_WORDS 80

WORD
vst_rotation (WORD handle, WORD angle)
{
  WORD result;

  intin[0] = angle;
  contrl[0] = 13;
  contrl[1] = 0;
  contrl[3] = 1;
  contrl[6] = handle;
  result = vdi ();
  if (result <= 0 || contrl[4] < 1)
    return 0;
  return intout[0];
}

WORD
vst_font (WORD handle, WORD font)
{
  WORD result;

  intin[0] = font;
  contrl[0] = 21;
  contrl[1] = 0;
  contrl[3] = 1;
  contrl[6] = handle;
  result = vdi ();
  if (result <= 0 || contrl[4] < 1)
    return 0;
  return intout[0];
}

WORD
vst_alignment (WORD handle, WORD horizontal, WORD vertical,
               WORD *horizontal_out, WORD *vertical_out)
{
  intin[0] = horizontal;
  intin[1] = vertical;
  contrl[0] = 39;
  contrl[1] = 0;
  contrl[3] = 2;
  contrl[6] = handle;
  if (vdi () <= 0 || contrl[4] < 2)
    return 0;
  if (horizontal_out != (WORD *) 0)
    *horizontal_out = intout[0];
  if (vertical_out != (WORD *) 0)
    *vertical_out = intout[1];
  return 1;
}

WORD
vst_effects (WORD handle, WORD effects)
{
  WORD result;

  intin[0] = effects;
  contrl[0] = 106;
  contrl[1] = 0;
  contrl[3] = 1;
  contrl[6] = handle;
  result = vdi ();
  if (result <= 0 || contrl[4] < 1)
    return 0;
  return intout[0];
}

WORD
vst_point (WORD handle, WORD point, WORD *character_width,
           WORD *character_height, WORD *cell_width, WORD *cell_height)
{
  WORD result;

  intin[0] = point;
  contrl[0] = 107;
  contrl[1] = 0;
  contrl[3] = 1;
  contrl[6] = handle;
  result = vdi ();
  if (result <= 0 || contrl[4] < 1 || contrl[2] < 2)
    return 0;
  if (character_width != (WORD *) 0)
    *character_width = ptsout[0];
  if (character_height != (WORD *) 0)
    *character_height = ptsout[1];
  if (cell_width != (WORD *) 0)
    *cell_width = ptsout[2];
  if (cell_height != (WORD *) 0)
    *cell_height = ptsout[3];
  return intout[0];
}

WORD
vqt_extent (WORD handle, BYTE string[], WORD extent[])
{
  UWORD count;
  WORD result;

  if (string == (BYTE *) 0 || extent == (WORD *) 0)
    return 0;
  count = 0;
  while (count < GEM_VDI_FONT_STRING_WORDS && string[count] != '\0')
    {
      intin[count] = (UWORD) (UBYTE) string[count];
      count++;
    }
  /* The fixed classic INTIN array cannot describe a longer string exactly. */
  if (count == GEM_VDI_FONT_STRING_WORDS && string[count] != '\0')
    return 0;
  contrl[0] = 116;
  contrl[1] = 0;
  contrl[3] = count;
  contrl[6] = handle;
  result = vdi ();
  if (result <= 0 || contrl[2] < 4)
    return 0;
  count = 0;
  while (count < 8U)
    {
      extent[count] = ptsout[count];
      count++;
    }
  return 1;
}

WORD
vqt_width (WORD handle, BYTE character, WORD *cell_width,
           WORD *left_delta, WORD *right_delta)
{
  WORD result;

  intin[0] = (UWORD) (UBYTE) character;
  contrl[0] = 117;
  contrl[1] = 0;
  contrl[3] = 1;
  contrl[6] = handle;
  result = vdi ();
  if (result <= 0 || contrl[4] < 1 || contrl[2] < 3)
    return 0;
  if (cell_width != (WORD *) 0)
    *cell_width = ptsout[0];
  if (left_delta != (WORD *) 0)
    *left_delta = ptsout[2];
  if (right_delta != (WORD *) 0)
    *right_delta = ptsout[4];
  return intout[0];
}

WORD
vst_load_fonts (WORD handle, WORD select)
{
  WORD result;

  intin[0] = select;
  contrl[0] = 119;
  contrl[1] = 0;
  contrl[3] = 1;
  contrl[6] = handle;
  result = vdi ();
  if (result <= 0 || contrl[4] < 1)
    return 0;
  return intout[0];
}

WORD
vst_unload_fonts (WORD handle, WORD select)
{
  intin[0] = select;
  contrl[0] = 120;
  contrl[1] = 0;
  contrl[3] = 1;
  contrl[6] = handle;
  return vdi () > 0 ? 1 : 0;
}

WORD
vqt_font_info (WORD handle, WORD *minimum_ade, WORD *maximum_ade,
               WORD distances[], WORD *maximum_width, WORD effects[])
{
  WORD result;

  contrl[0] = 131;
  contrl[1] = 0;
  contrl[3] = 0;
  contrl[6] = handle;
  result = vdi ();
  if (result <= 0 || contrl[4] < 2 || contrl[2] < 5)
    return 0;
  if (minimum_ade != (WORD *) 0)
    *minimum_ade = intout[0];
  if (maximum_ade != (WORD *) 0)
    *maximum_ade = intout[1];
  if (distances != (WORD *) 0)
    {
      distances[0] = ptsout[1];
      distances[1] = ptsout[3];
      distances[2] = ptsout[5];
      distances[3] = ptsout[7];
      distances[4] = ptsout[9];
    }
  if (maximum_width != (WORD *) 0)
    *maximum_width = ptsout[0];
  if (effects != (WORD *) 0)
    {
      effects[0] = ptsout[2];
      effects[1] = ptsout[4];
      effects[2] = ptsout[6];
    }
  return 1;
}

WORD
vqt_fontinfo (WORD handle, WORD *minimum_ade, WORD *maximum_ade,
              WORD distances[], WORD *maximum_width, WORD effects[])
{
  return vqt_font_info (handle, minimum_ade, maximum_ade, distances,
                        maximum_width, effects);
}
