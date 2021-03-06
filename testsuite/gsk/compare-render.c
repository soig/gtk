#include <string.h>
#include <glib/gstdio.h>
#include <gtk/gtk.h>
#include <stdlib.h>
#include "reftest-compare.h"

char *
file_replace_extension (const char *old_file,
                        const char *old_ext,
                        const char *new_ext)
{
  GString *file = g_string_new (NULL);

  if (g_str_has_suffix (old_file, old_ext))
    g_string_append_len (file, old_file, strlen (old_file) - strlen (old_ext));
  else
    g_string_append (file, old_file);

  g_string_append (file, new_ext);

  return g_string_free (file, FALSE);
}

static char *
get_output_file (const char *file,
                 const char *orig_ext,
                 const char *new_ext)
{
  const char *dir;
  char *result, *base;
  char *name;

  dir = g_get_tmp_dir ();
  base = g_path_get_basename (file);
  name = file_replace_extension (base, orig_ext, new_ext);

  result = g_strconcat (dir, G_DIR_SEPARATOR_S, name, NULL);

  g_free (base);
  g_free (name);

  return result;
}

static void
save_image (cairo_surface_t *surface,
            const char      *test_name,
            const char      *extension)
{
  char *filename = get_output_file (test_name, ".node", extension);

  g_print ("Storing test result image at %s\n", filename);
  g_assert (cairo_surface_write_to_png (surface, filename) == CAIRO_STATUS_SUCCESS);
  g_free (filename);
}

static void
deserialize_error_func (const GtkCssSection *section,
                        const GError        *error,
                        gpointer             user_data)
{
  char *section_str = gtk_css_section_to_string (section);

  g_print ("Error at %s: %s", section_str, error->message);
  *((gboolean *) user_data) = FALSE;

  free (section_str);
}

/*
 * Arguments:
 *   1) .node file to compare
 *   2) .png file to compare the rendered .node file to
 */
int
main (int argc, char **argv)
{
  cairo_surface_t *reference_surface = NULL;
  cairo_surface_t *rendered_surface = NULL;
  cairo_surface_t *diff_surface = NULL;
  GdkTexture *texture;
  GskRenderer *renderer;
  GdkSurface *window;
  GskRenderNode *node;
  const char *node_file;
  const char *png_file;
  gboolean success = TRUE;

  g_assert (argc == 3);

  gtk_init ();

  node_file = argv[1];
  png_file = argv[2];

  window = gdk_surface_new_toplevel (gdk_display_get_default(), 10 , 10);
  renderer = gsk_renderer_new_for_surface (window);

  g_print ("Node file: '%s'\n", node_file);
  g_print ("PNG file: '%s'\n", png_file);

  /* Load the render node from the given .node file */
  {
    GBytes *bytes;
    GError *error = NULL;
    gsize len;
    char *contents;

    if (!g_file_get_contents (node_file, &contents, &len, &error))
      {
        g_print ("Could not open node file: %s\n", error->message);
        g_clear_error (&error);
        return 1;
      }

    bytes = g_bytes_new_take (contents, len);
    node = gsk_render_node_deserialize (bytes, deserialize_error_func, &success);
    g_bytes_unref (bytes);

    g_assert_no_error (error);
    g_assert (node != NULL);
  }

  /* Render the .node file and download to cairo surface */
  texture = gsk_renderer_render_texture (renderer, node, NULL);
  g_assert (texture != NULL);

  rendered_surface = cairo_image_surface_create (CAIRO_FORMAT_ARGB32,
                                                 gdk_texture_get_width (texture),
                                                 gdk_texture_get_height (texture));
  gdk_texture_download (texture,
                        cairo_image_surface_get_data (rendered_surface),
                        cairo_image_surface_get_stride (rendered_surface));
  cairo_surface_mark_dirty (rendered_surface);

  /* Load the given reference png file */
  reference_surface = cairo_image_surface_create_from_png (png_file);
  if (cairo_surface_status (reference_surface))
    {
      g_print ("Error loading reference surface: %s\n",
               cairo_status_to_string (cairo_surface_status (reference_surface)));
      success = FALSE;
    }
  else
    {
      /* Now compare the two */
      diff_surface = reftest_compare_surfaces (rendered_surface, reference_surface);

      if (diff_surface)
        {
          save_image (diff_surface, node_file, ".diff.png");
          cairo_surface_destroy (diff_surface);
          success = FALSE;
        }
    }

  save_image (rendered_surface, node_file, ".out.png");

  cairo_surface_destroy (reference_surface);
  cairo_surface_destroy (rendered_surface);
  g_object_unref (texture);

  return success ? 0 : 1;
}
