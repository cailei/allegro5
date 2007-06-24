#include <string.h>
#include "allegro.h"
#include "internal/aintern.h"
#include ALLEGRO_INTERNAL_HEADER
#include "internal/aintern_display.h"
#include "internal/aintern_bitmap.h"

/* Bitmap state is thread local */

static AL_COLOR _mask_color = { { 0, 0, 0, 0 } };

/* Creates a memory bitmap. A memory bitmap can only be drawn to other memory
 * bitmaps, not to a display.
 */
static AL_BITMAP *_al_create_memory_bitmap(int w, int h)
{
   AL_BITMAP *bitmap = _AL_MALLOC(sizeof *bitmap);
   memset(bitmap, 0, sizeof *bitmap);
   bitmap->format = al_get_new_bitmap_format();
   bitmap->flags = al_get_new_bitmap_flags();
   bitmap->w = w;
   bitmap->h = h;
   bitmap->display = NULL;
   bitmap->locked = false;
   // FIXME: Of course, we do need to handle all the possible different formats,
   // this will easily fill up its own file of 1000 lines, but for now,
   // RGBA with 8-bit per component is hardcoded.
   bitmap->memory = _AL_MALLOC(w * h * _al_get_pixel_size(al_get_new_bitmap_format()));
   memset(bitmap->memory, 0, w * h * _al_get_pixel_size(al_get_new_bitmap_format()));
   return bitmap;
}

static void _al_destroy_memory_bitmap(AL_BITMAP *bmp)
{
   _AL_FREE(bmp->memory);
   _AL_FREE(bmp);
}

/* Creates an empty display bitmap. A display bitmap can only be drawn to a
 * display.
 */
AL_BITMAP *al_create_bitmap(int w, int h)
{
   AL_BITMAP *bitmap;
   
   if (al_get_new_bitmap_flags() & AL_MEMORY_BITMAP) {
   	return _al_create_memory_bitmap(w, h);
   }

   /* Else it's a display bitmap */

   bitmap = _al_current_display->vt->create_bitmap(_al_current_display, w, h);

   bitmap->display = _al_current_display;
   bitmap->w = w;
   bitmap->h = h;
   bitmap->locked = false;

   if (!bitmap->memory) {
   	bitmap->memory = _AL_MALLOC(w * h * _al_get_pixel_size(bitmap->format));
        memset(bitmap->memory, 0, w * h * _al_get_pixel_size(bitmap->format));
   }

   bitmap->vt->upload_bitmap(bitmap, 0, 0, w, h);

   return bitmap;
}

/* Destroy a (memory or display) bitmap. The passed pointer will be invalid
 * afterwards, so best set it to NULL.
 */
void al_destroy_bitmap(AL_BITMAP *bitmap)
{
   if (bitmap->flags & AL_MEMORY_BITMAP) {
   	_al_destroy_memory_bitmap(bitmap);
	return;
   }

   /* Else it's a display bitmap */

   if (bitmap->locked)
      al_unlock_bitmap(bitmap);

   if (bitmap->vt)
      bitmap->vt->destroy_bitmap(bitmap);

   if (bitmap->memory)
      _AL_FREE(bitmap->memory);

   _AL_FREE(bitmap);
}

/* TODO
 * This will load a bitmap from a file into a memory bitmap, keeping it in the
 * same format as on disk. That is, a paletted image will be stored as indices
 * plus the palette, 16-bit color channels stay as 16-bit, and so on. If
 * a format is not supported by Allegro, the closest one will be used. (E.g. we
 * likely will throw away things like frame timings or gamma correction.)
 */
static AL_BITMAP *_al_load_memory_bitmap(char const *filename)
{
   // TODO:
   // The idea is, load_bitmap returns a memory representation of the bitmap,
   // maybe in fixed RGBA or whatever format is set as default (e.g. for HDR
   // images, should not reduce to 8-bit channels).
   // Then, it is converted to a display specific bitmap, which can be used
   // for blitting to the current display.
   set_color_conversion(COLORCONV_NONE);
   // FIXME: should not use the 4.2 function here of course
   PALETTE pal;
   BITMAP *file_data = load_bitmap(filename, pal);
   select_palette(pal);
   if (!file_data)
      return NULL;
   AL_BITMAP *bitmap = al_create_bitmap(file_data->w, file_data->h);

   _al_convert_compat_bitmap(
   	file_data,
	bitmap->memory, bitmap->format, _al_get_pixel_size(bitmap->format)*bitmap->w,
	0, 0, 0, 0,
	file_data->w, file_data->h);

   destroy_bitmap(file_data);
   return bitmap;
}

/* Load a bitmap from a file into a display bitmap, ready to be drawn.
 */
AL_BITMAP *al_load_bitmap(char const *filename)
{
   AL_BITMAP *bitmap;
   
   if (al_get_new_bitmap_flags() & AL_MEMORY_BITMAP) {
   	return _al_load_memory_bitmap(filename);
   }

   /* Else it's a display bitmap */

   bitmap = _al_load_memory_bitmap(filename);

   bitmap->vt->upload_bitmap(bitmap, 0, 0, bitmap->w, bitmap->h);

   return bitmap;
}

void al_draw_bitmap(AL_BITMAP *bitmap, float dx, float dy, int flags)
{
   AL_BITMAP *dest = al_get_target_bitmap();

   /* If one is a memory bitmap, do memory blit */
   if ((bitmap->flags & AL_MEMORY_BITMAP) || (dest->flags & AL_MEMORY_BITMAP)) {
      _al_draw_bitmap_memory(bitmap, dx, dy, flags);
   }
   else if (al_is_compatible_bitmap(bitmap))
      bitmap->vt->draw_bitmap(bitmap, dx, dy, flags);
}

void al_draw_bitmap_region(AL_BITMAP *bitmap, float sx, float sy,
	float sw, float sh, float dx, float dy, int flags)
{
   AL_BITMAP *dest = al_get_target_bitmap();

   /* If one is a memory bitmap, do memory blit */
   if ((bitmap->flags & AL_MEMORY_BITMAP) || (dest->flags & AL_MEMORY_BITMAP)) {
      _al_draw_bitmap_region_memory(bitmap, sx, sy, sw, sh, dx, dy, flags);
   }
   else if (al_is_compatible_bitmap(bitmap))
      bitmap->vt->draw_bitmap_region(bitmap, sx, sy, sw, sh, dx, dy, flags);
}

void al_draw_scaled_bitmap(AL_BITMAP *bitmap, float sx, float sy,
	float sw, float sh, float dx, float dy, float dw, float dh, int flags)
{
   AL_BITMAP *dest = al_get_target_bitmap();

   if ((bitmap->flags & AL_MEMORY_BITMAP) || (dest->flags & AL_MEMORY_BITMAP)) {
      _al_draw_scaled_bitmap_memory(bitmap, sx, sy, sw, sh,
         dx, dy, dw, dh, flags);
   }
   else if (al_is_compatible_bitmap(bitmap))
      bitmap->vt->draw_scaled_bitmap(bitmap, sx, sy, sw, sh,
         dx, dy, dw, dh, flags);
}

void al_draw_rotated_bitmap(AL_BITMAP *bitmap, float cx, float cy,
	float dx, float dy, float angle, int flags)
{
   AL_BITMAP *dest = al_get_target_bitmap();

   /* If one is a memory bitmap, do memory blit */
   if ((bitmap->flags & AL_MEMORY_BITMAP) || (dest->flags & AL_MEMORY_BITMAP)) {
      _al_draw_rotated_bitmap_memory(bitmap, cx, cy,
         dx, dy, angle, flags);
   }
   else if (al_is_compatible_bitmap(bitmap))
      bitmap->vt->draw_rotated_bitmap(bitmap, cx, cy, dx, dy, angle, flags);
}

void al_draw_rotated_scaled_bitmap(AL_BITMAP *bitmap, float cx, float cy,
	float dx, float dy, float xscale, float yscale, float angle,
	int flags)
{
   AL_BITMAP *dest = al_get_target_bitmap();

   /* If one is a memory bitmap, do memory blit */
   if ((bitmap->flags & AL_MEMORY_BITMAP) || (dest->flags & AL_MEMORY_BITMAP)) {
      _al_draw_rotated_scaled_bitmap_memory(bitmap, cx, cy,
         dx, dy, xscale, yscale, angle, flags);
   }
   else if (al_is_compatible_bitmap(bitmap))
      bitmap->vt->draw_rotated_scaled_bitmap(bitmap, cx, cy,
         dx, dy, xscale, yscale, angle, flags);
}

AL_LOCKED_REGION *al_lock_bitmap_region(AL_BITMAP *bitmap,
	int x, int y,
	int width, int height,
	AL_LOCKED_REGION *locked_region,
	int flags)
{
	if (bitmap->locked)
		return NULL;

	bitmap->locked = true;
	bitmap->lock_x = x;
	bitmap->lock_y = y;
	bitmap->lock_w = width;
	bitmap->lock_h = height;
	bitmap->lock_flags = flags;

	if (bitmap->flags & AL_MEMORY_BITMAP || bitmap->flags & AL_SYNC_MEMORY_COPY) {
		locked_region->data = bitmap->memory+(bitmap->w*y+x)*_al_get_pixel_size(bitmap->format);
		locked_region->format = bitmap->format;
		locked_region->pitch = bitmap->w*_al_get_pixel_size(bitmap->format);
	}
	else {
		locked_region = bitmap->vt->lock_region(bitmap,
			x, y, width, height,
			locked_region,
			flags);
	}

	if (locked_region)
		memcpy(&bitmap->locked_region, locked_region, sizeof(AL_LOCKED_REGION));

	return locked_region;
}

AL_LOCKED_REGION *al_lock_bitmap(AL_BITMAP *bitmap,
	AL_LOCKED_REGION *locked_region,
	int flags)
{
	return al_lock_bitmap_region(bitmap, 0, 0, bitmap->w, bitmap->h,
		locked_region, flags);
}

void al_unlock_bitmap(AL_BITMAP *bitmap)
{
	if (bitmap->flags & AL_SYNC_MEMORY_COPY && !(bitmap->flags & AL_MEMORY_BITMAP)) {
		bitmap->vt->upload_bitmap(bitmap,
			bitmap->lock_x,
			bitmap->lock_y,
			bitmap->lock_w,
			bitmap->lock_h);
	}
	else if (!(bitmap->flags & AL_MEMORY_BITMAP)) {
		bitmap->vt->unlock_region(bitmap);
	}

	bitmap->locked = false;
}

AL_COLOR *al_get_mask_color(AL_COLOR *color)
{
	memcpy(color, &_mask_color, sizeof(AL_COLOR));
	return color;
}

void al_set_mask_color(AL_COLOR *color)
{
	memcpy(&_mask_color, color, sizeof(AL_COLOR));
}

void al_convert_mask_to_alpha(AL_BITMAP *bitmap, AL_COLOR *mask_color)
{
	AL_LOCKED_REGION lr;
	int x, y;
	AL_COLOR pixel;
	AL_COLOR alpha_pixel;

	if (!al_lock_bitmap(bitmap, &lr, 0)) {
		TRACE("al_convert_mask_to_alpha: Couldn't lock bitmap.\n");
		return;
	}

	_al_push_target_bitmap();
	al_set_target_bitmap(bitmap);

	al_map_rgba(bitmap, &alpha_pixel, 0, 0, 0, 0);

	for (y = 0; y < bitmap->h; y++) {
		for (x = 0; x < bitmap->w; x++) {
			al_get_pixel(bitmap, x, y, &pixel);
			if (memcmp(&pixel, mask_color, sizeof(AL_COLOR)) == 0) {
				al_put_pixel(x, y, &alpha_pixel);
			}
		}
	}

	_al_pop_target_bitmap();

	al_unlock_bitmap(bitmap);
}

