/*         ______   ___    ___
 *        /\  _  \ /\_ \  /\_ \
 *        \ \ \L\ \\//\ \ \//\ \      __     __   _ __   ___
 *         \ \  __ \ \ \ \  \ \ \   /'__`\ /'_ `\/\`'__\/ __`\
 *          \ \ \/\ \ \_\ \_ \_\ \_/\  __//\ \L\ \ \ \//\ \L\ \
 *           \ \_\ \_\/\____\/\____\ \____\ \____ \ \_\\ \____/
 *            \/_/\/_/\/____/\/____/\/____/\/___L\ \/_/ \/___/
 *                                           /\____/
 *                                           \_/__/
 *
 *      Software point implementation functions.
 *
 *
 *      By Pavel Sountsov.
 *
 *      See readme.txt for copyright information.
 */


#include "allegro5/allegro_primitives.h"
#include "allegro5/internal/aintern.h"
#include "allegro5/internal/aintern_prim.h"
#include "allegro5/internal/aintern_prim_soft.h"
#include <math.h>

static int fix_var(float var, int max_var)
{
   const int ivar = (int)floorf(var);
   const int ret = ivar % max_var;
   if(ret >= 0)
      return ret;
   else
      return ret + max_var;
}

void _al_point_2d(ALLEGRO_BITMAP* texture, ALLEGRO_VERTEX* v)
{
   int shade = 1;
   int op, src_mode, dst_mode;
   ALLEGRO_COLOR ic, vc;

   vc = al_get_allegro_color(v->color);

   al_get_blender(&op, &src_mode, &dst_mode, &ic);
   if (src_mode == ALLEGRO_ONE && dst_mode == ALLEGRO_ZERO &&
         ic.r == 1.0f && ic.g == 1.0f && ic.b == 1.0f && ic.a == 1.0f) {
      shade = 0;
   }
   
   if (texture) {
      float U = fix_var(v->u, al_get_bitmap_width(texture));
      float V = fix_var(v->v, al_get_bitmap_height(texture));
      ALLEGRO_COLOR color = al_get_pixel(texture, U, V);

      if(vc.r != 1 || vc.g != 1 || vc.b != 1 || vc.a != 1) {
         color.r *= vc.r;
         color.g *= vc.g;
         color.b *= vc.b;
         color.a *= vc.a;
      }

      if (shade) {
         al_draw_pixel(v->x, v->y, color);
      } else {
         al_put_pixel(v->x, v->y, color);
      }
   } else {
      ALLEGRO_COLOR color = al_map_rgba_f(vc.r, vc.g, vc.b, vc.a);
      if (shade) {
         al_draw_pixel(v->x, v->y, color);
      } else {
         al_put_pixel(v->x, v->y, color);
      }
   }
}