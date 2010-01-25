/* libjpeg wrapper for Allegro 5 iio addon.
 * by Elias Pschernig
 */

#include <stdio.h>
#include <stdlib.h>
#include <setjmp.h>
#ifdef ALLEGRO_HAVE_STDINT_H
#include <stdint.h>
#endif

#define BUFFER_SIZE 4096

#include "allegro5/allegro5.h"
#include "allegro5/fshook.h"
#include "allegro5/internal/aintern_memory.h"
#include "allegro5/allegro_image.h"

#include "iio.h"

/* HAVE_BOOLEAN must be defined before including jpeg headers */
#ifdef __MINGW32__
#define HAVE_BOOLEAN
#endif
#include <jpeglib.h>
#include <jerror.h>

struct my_src_mgr
{
   struct jpeg_source_mgr pub;
   unsigned char *buffer;
   ALLEGRO_FILE *fp;
};

struct my_dest_mgr
{
   struct jpeg_destination_mgr pub;
   unsigned char *buffer;
   ALLEGRO_FILE *fp;
};

struct my_err_mgr
{
   struct jpeg_error_mgr pub;
   jmp_buf jmpenv;
};

static void init_source(j_decompress_ptr cinfo)
{
   (void)cinfo;
}

static void init_destination(j_compress_ptr cinfo)
{
   struct my_dest_mgr *dest = (void *)cinfo->dest;
   dest->pub.next_output_byte = dest->buffer;
   dest->pub.free_in_buffer = BUFFER_SIZE;
}

static boolean fill_input_buffer(j_decompress_ptr cinfo)
{
   struct my_src_mgr *src = (void *)cinfo->src;
   src->pub.next_input_byte = src->buffer;
   src->pub.bytes_in_buffer = al_fread(src->fp, src->buffer, BUFFER_SIZE);
   return 1;
}

static boolean empty_output_buffer(j_compress_ptr cinfo)
{
   struct my_dest_mgr *dest = (void *)cinfo->dest;
   al_fwrite(dest->fp, dest->buffer, BUFFER_SIZE);
   dest->pub.next_output_byte = dest->buffer;
   dest->pub.free_in_buffer = BUFFER_SIZE;
   return 1;
}

static void skip_input_data(j_decompress_ptr cinfo, long num_bytes)
{
   struct my_src_mgr *src = (void *)cinfo->src;
   if (num_bytes <= (long)src->pub.bytes_in_buffer) {
      src->pub.next_input_byte += num_bytes;
      src->pub.bytes_in_buffer -= num_bytes;
   }
   else {
      long skip = num_bytes - src->pub.bytes_in_buffer;
      al_fseek(src->fp, skip, ALLEGRO_SEEK_CUR);
      src->pub.bytes_in_buffer = 0;
   }
}

static void term_source(j_decompress_ptr cinfo)
{
   (void)cinfo;
}

static void term_destination(j_compress_ptr cinfo)
{
   struct my_dest_mgr *dest = (void *)cinfo->dest;
   al_fwrite(dest->fp, dest->buffer, BUFFER_SIZE - dest->pub.free_in_buffer);
}


static void jpeg_packfile_src(j_decompress_ptr cinfo, ALLEGRO_FILE *fp,
                              unsigned char *buffer)
{
   struct my_src_mgr *src;

   if (!cinfo->src)
      cinfo->src =
          (*cinfo->mem->alloc_small) ((void *)cinfo, JPOOL_PERMANENT,
                                      sizeof *src);

   src = (void *)cinfo->src;
   src->pub.init_source = init_source;
   src->pub.fill_input_buffer = fill_input_buffer;
   src->pub.skip_input_data = skip_input_data;
   src->pub.resync_to_restart = jpeg_resync_to_restart;
   src->pub.term_source = term_source;
   src->pub.bytes_in_buffer = 0;
   src->buffer = buffer;
   src->fp = fp;
}

static void jpeg_packfile_dest(j_compress_ptr cinfo, ALLEGRO_FILE *fp,
                               unsigned char *buffer)
{
   struct my_dest_mgr *dest;

   if (!cinfo->dest)
      cinfo->dest =
          (*cinfo->mem->alloc_small) ((void *)cinfo, JPOOL_PERMANENT,
                                      sizeof *dest);

   dest = (void *)cinfo->dest;
   dest->pub.init_destination = init_destination;
   dest->pub.empty_output_buffer = empty_output_buffer;
   dest->pub.term_destination = term_destination;
   dest->pub.free_in_buffer = 0;
   dest->buffer = buffer;
   dest->fp = fp;
}

static void my_error_exit(j_common_ptr cinfo)
{
   struct my_err_mgr *jerr = (void *)cinfo->err;

   longjmp(jerr->jmpenv, 1);
}

/* We keep data for load_jpg_entry_helper in a structure allocated in the
 * caller's stack frame to avoid problems with automatic variables being
 * undefined after a longjmp.
 */
struct load_jpg_entry_helper_data {
   bool error;
   ALLEGRO_BITMAP *bmp;
   unsigned char *buffer;
   unsigned char *row;
};

static void load_jpg_entry_helper(ALLEGRO_FILE *fp,
   struct load_jpg_entry_helper_data *data)
{
   struct jpeg_decompress_struct cinfo;
   struct my_err_mgr jerr;
   ALLEGRO_LOCKED_REGION *lock;
   int w, h, s;

   data->error = false;

   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   if (setjmp(jerr.jmpenv) != 0) {
      /* Longjmp'd. */
      data->error = true;
      goto longjmp_error;
   }

   data->buffer = _AL_MALLOC(BUFFER_SIZE);
   if (!data->buffer) {
      data->error = true;
      goto error;
   }

   jpeg_create_decompress(&cinfo);
   jpeg_packfile_src(&cinfo, fp, data->buffer);
   jpeg_read_header(&cinfo, true);
   jpeg_start_decompress(&cinfo);

   w = cinfo.output_width;
   h = cinfo.output_height;
   s = cinfo.output_components;

   /* Only one and three components make sense in a JPG file. */
   if (s != 1 && s != 3) {
      data->error = true;
      goto error;
   }

   data->bmp = al_create_bitmap(w, h);
   if (!data->bmp) {
      data->error = true;
      goto error;
   }

   /* Allegro's pixel format is endian independent, so that in
    * ALLEGRO_PIXEL_FORMAT_RGB_888 the lower 8 bits always hold the Blue
    * component.  On a little endian system this is in byte 0.  On a big
    * endian system this is in byte 2.
    *
    * libjpeg expects byte 0 to hold the Red component, byte 1 to hold the
    * Green component, byte 2 to hold the Blue component.  Hence on little
    * endian systems we need the opposite format, ALLEGRO_PIXEL_FORMAT_BGR_888.
    */
#ifdef ALLEGRO_BIG_ENDIAN
   lock = al_lock_bitmap(data->bmp, ALLEGRO_PIXEL_FORMAT_RGB_888,
       ALLEGRO_LOCK_WRITEONLY);
#else
   lock = al_lock_bitmap(data->bmp, ALLEGRO_PIXEL_FORMAT_BGR_888,
       ALLEGRO_LOCK_WRITEONLY);
#endif
   al_set_target_bitmap(data->bmp);

   if (s == 3) {
      /* Colour. */
      int y;

      for (y = cinfo.output_scanline; y < h; y = cinfo.output_scanline) {
         unsigned char *out[1];
         out[0] = ((unsigned char *)lock->data) + y * lock->pitch;
         jpeg_read_scanlines(&cinfo, (void *)out, 1);
      }
   }
   else if (s == 1) {
      /* Greyscale. */
      unsigned char *in;
      unsigned char *out;
      int x, y;

      data->row = _AL_MALLOC(w);
      for (y = cinfo.output_scanline; y < h; y = cinfo.output_scanline) {
         jpeg_read_scanlines(&cinfo, (void *)&data->row, 1);
         in = data->row;
         out = ((unsigned char *)lock->data) + y * lock->pitch;
         for (x = 0; x < w; x++) {
            *out++ = *in;
            *out++ = *in;
            *out++ = *in;
            in++;
         }
      }
   }

 error:
   jpeg_finish_decompress(&cinfo);

 longjmp_error:
   jpeg_destroy_decompress(&cinfo);

   if (data->bmp) {
      if (al_is_bitmap_locked(data->bmp)) {
         al_unlock_bitmap(data->bmp);
      }
      if (data->error) {
         al_destroy_bitmap(data->bmp);
         data->bmp = NULL;
      }
   }

   _AL_FREE(data->buffer);
   _AL_FREE(data->row);
}

ALLEGRO_BITMAP *al_load_jpg_stream(ALLEGRO_FILE *fp)
{
   ALLEGRO_STATE state;
   struct load_jpg_entry_helper_data data;

   al_store_state(&state, ALLEGRO_STATE_TARGET_BITMAP);

   memset(&data, 0, sizeof(data));
   load_jpg_entry_helper(fp, &data);

   al_restore_state(&state);

   return data.bmp;
}

/* See comment about load_jpg_entry_helper_data. */
struct save_jpg_entry_helper_data {
   bool error;
   unsigned char *buffer;
};

static void save_jpg_entry_helper(ALLEGRO_FILE *fp, ALLEGRO_BITMAP *bmp,
   struct save_jpg_entry_helper_data *data)
{
   struct jpeg_compress_struct cinfo;
   struct my_err_mgr jerr;
   ALLEGRO_LOCKED_REGION *lock;

   data->error = false;

   cinfo.err = jpeg_std_error(&jerr.pub);
   jerr.pub.error_exit = my_error_exit;
   if (setjmp(jerr.jmpenv)) {
      /* Longjmp'd. */
      data->error = true;
      goto longjmp_error;
   }

   data->buffer = _AL_MALLOC(BUFFER_SIZE);
   if (!data->buffer) {
      data->error = true;
      goto error;
   }

   jpeg_create_compress(&cinfo);
   jpeg_packfile_dest(&cinfo, fp, data->buffer);

   cinfo.image_width = al_get_bitmap_width(bmp);
   cinfo.image_height = al_get_bitmap_height(bmp);
   cinfo.input_components = 3;
   cinfo.in_color_space = JCS_RGB;
   jpeg_set_defaults(&cinfo);

   jpeg_start_compress(&cinfo, 1);

   /* See comment in load_jpg_entry_helper. */
#ifdef ALLEGRO_BIG_ENDIAN
   lock = al_lock_bitmap(bmp, ALLEGRO_PIXEL_FORMAT_RGB_888,
      ALLEGRO_LOCK_READONLY);
#else
   lock = al_lock_bitmap(bmp, ALLEGRO_PIXEL_FORMAT_BGR_888,
      ALLEGRO_LOCK_READONLY);
#endif
   al_set_target_bitmap(bmp);

   while (cinfo.next_scanline < cinfo.image_height) {
      unsigned char *row[1];
      row[0] = ((unsigned char *)lock->data)
         + cinfo.next_scanline * lock->pitch;
      jpeg_write_scanlines(&cinfo, (void *)row, 1);
   }

 error:
   jpeg_finish_compress(&cinfo);

 longjmp_error:
   jpeg_destroy_compress(&cinfo);

   if (al_is_bitmap_locked(bmp)) {
      al_unlock_bitmap(bmp);
   }

   _AL_FREE(data->buffer);
}

bool al_save_jpg_stream(ALLEGRO_FILE *fp, ALLEGRO_BITMAP *bmp)
{
   ALLEGRO_STATE state;
   struct save_jpg_entry_helper_data data;

   al_store_state(&state, ALLEGRO_STATE_TARGET_BITMAP);

   memset(&data, 0, sizeof(data));
   save_jpg_entry_helper(fp, bmp, &data);

   al_restore_state(&state);

   return !data.error;
}

/* Function: al_load_jpg
 */
ALLEGRO_BITMAP *al_load_jpg(char const *filename)
{
   ALLEGRO_FILE *fp;
   ALLEGRO_BITMAP *bmp;

   ASSERT(filename);

   fp = al_fopen(filename, "rb");
   if (!fp)
      return NULL;

   bmp = al_load_jpg_stream(fp);

   al_fclose(fp);

   return bmp;
}

/* Function: al_save_jpg
 */
bool al_save_jpg(char const *filename, ALLEGRO_BITMAP *bmp)
{
   ALLEGRO_FILE *fp;
   bool result;

   ASSERT(filename);
   ASSERT(bmp);

   fp = al_fopen(filename, "wb");
   if (!fp) {
      TRACE("Unable to open file %s for writing\n", filename);
      return false;
   }

   result = al_save_jpg_stream(fp, bmp);

   al_fclose(fp);

   return result;
}

/* vim: set sts=3 sw=3 et: */