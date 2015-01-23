/*
 * GStreamer
 * Copyright (C) <1999> Erik Walthinsen <omega@cse.ogi.edu>
 * Copyright (C) 2002,2007 David A. Schleef <ds@schleef.org>
 * Copyright (C) 2008 Julien Isorce <julien.isorce@gmail.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

/**
 * SECTION:element-gltestsrc
 *
 * <refsect2>
 * <para>
 * The gltestsrc element is used to produce test video texture.
 * The video test produced can be controlled with the "pattern"
 * property.
 * </para>
 * <title>Example launch line</title>
 * <para>
 * <programlisting>
 * gst-launch -v gltestsrc pattern=smpte ! glimagesink
 * </programlisting>
 * Shows original SMPTE color bars in a window.
 * </para>
 * </refsect2>
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include "gstgltestsrc.h"
#include "gltestsrc.h"
#include <gst/gst-i18n-plugin.h>

#if GST_GL_HAVE_PLATFORM_EGL
#include <gst/gl/egl/gsteglimagememory.h>
#endif

#define USE_PEER_BUFFERALLOC
#define SUPPORTED_GL_APIS GST_GL_API_OPENGL

GST_DEBUG_CATEGORY_STATIC (gl_test_src_debug);
#define GST_CAT_DEFAULT gl_test_src_debug

enum
{
  PROP_0,
  PROP_PATTERN,
  PROP_TIMESTAMP_OFFSET,
  PROP_IS_LIVE
      /* FILL ME */
};

static GstStaticPadTemplate src_factory = GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS (GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_MEMORY_GL_MEMORY,
            "RGBA") "; "
#if GST_GL_HAVE_PLATFORM_EGL
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES (GST_CAPS_FEATURE_MEMORY_EGL_IMAGE,
            "RGBA") "; "
#endif
        GST_VIDEO_CAPS_MAKE_WITH_FEATURES
        (GST_CAPS_FEATURE_META_GST_VIDEO_GL_TEXTURE_UPLOAD_META,
            "RGBA") "; " GST_VIDEO_CAPS_MAKE (GST_GL_COLOR_CONVERT_FORMATS))
    );

#define gst_gl_test_src_parent_class parent_class
G_DEFINE_TYPE (GstGLTestSrc, gst_gl_test_src, GST_TYPE_PUSH_SRC);

static void gst_gl_test_src_set_pattern (GstGLTestSrc * gltestsrc,
    int pattern_type);
static void gst_gl_test_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec);
static void gst_gl_test_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec);
static void gst_gl_test_src_dispose (GObject * object);

static gboolean gst_gl_test_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps);
static GstCaps *gst_gl_test_src_fixate (GstBaseSrc * bsrc, GstCaps * caps);

static gboolean gst_gl_test_src_is_seekable (GstBaseSrc * psrc);
static gboolean gst_gl_test_src_do_seek (GstBaseSrc * bsrc,
    GstSegment * segment);
static gboolean gst_gl_test_src_query (GstBaseSrc * bsrc, GstQuery * query);
static void gst_gl_test_src_set_context (GstElement * element,
    GstContext * context);

static void gst_gl_test_src_get_times (GstBaseSrc * basesrc,
    GstBuffer * buffer, GstClockTime * start, GstClockTime * end);
static GstFlowReturn gst_gl_test_src_fill (GstPushSrc * psrc,
    GstBuffer * buffer);
static gboolean gst_gl_test_src_start (GstBaseSrc * basesrc);
static gboolean gst_gl_test_src_stop (GstBaseSrc * basesrc);
static gboolean gst_gl_test_src_decide_allocation (GstBaseSrc * basesrc,
    GstQuery * query);

static void gst_gl_test_src_callback (gpointer stuff);

static gboolean gst_gl_test_src_init_shader (GstGLTestSrc * gltestsrc);

#define GST_TYPE_GL_TEST_SRC_PATTERN (gst_gl_test_src_pattern_get_type ())
static GType
gst_gl_test_src_pattern_get_type (void)
{
  static GType gl_test_src_pattern_type = 0;
  static const GEnumValue pattern_types[] = {
    {GST_GL_TEST_SRC_SMPTE, "SMPTE 100% color bars", "smpte"},
    {GST_GL_TEST_SRC_SNOW, "Random (television snow)", "snow"},
    {GST_GL_TEST_SRC_BLACK, "100% Black", "black"},
    {GST_GL_TEST_SRC_WHITE, "100% White", "white"},
    {GST_GL_TEST_SRC_RED, "Red", "red"},
    {GST_GL_TEST_SRC_GREEN, "Green", "green"},
    {GST_GL_TEST_SRC_BLUE, "Blue", "blue"},
    {GST_GL_TEST_SRC_CHECKERS1, "Checkers 1px", "checkers-1"},
    {GST_GL_TEST_SRC_CHECKERS2, "Checkers 2px", "checkers-2"},
    {GST_GL_TEST_SRC_CHECKERS4, "Checkers 4px", "checkers-4"},
    {GST_GL_TEST_SRC_CHECKERS8, "Checkers 8px", "checkers-8"},
    {GST_GL_TEST_SRC_CIRCULAR, "Circular", "circular"},
    {GST_GL_TEST_SRC_BLINK, "Blink", "blink"},
    {GST_GL_TEST_SRC_MANDELBROT, "Mandelbrot Fractal", "mandelbrot"},
    {0, NULL, NULL}
  };

  if (!gl_test_src_pattern_type) {
    gl_test_src_pattern_type =
        g_enum_register_static ("GstGLTestSrcPattern", pattern_types);
  }
  return gl_test_src_pattern_type;
}

static void
gst_gl_test_src_class_init (GstGLTestSrcClass * klass)
{
  GObjectClass *gobject_class;
  GstBaseSrcClass *gstbasesrc_class;
  GstPushSrcClass *gstpushsrc_class;
  GstElementClass *element_class;

  GST_DEBUG_CATEGORY_INIT (gl_test_src_debug, "gltestsrc", 0,
      "Video Test Source");

  gobject_class = (GObjectClass *) klass;
  gstbasesrc_class = (GstBaseSrcClass *) klass;
  gstpushsrc_class = (GstPushSrcClass *) klass;
  element_class = GST_ELEMENT_CLASS (klass);

  gobject_class->set_property = gst_gl_test_src_set_property;
  gobject_class->get_property = gst_gl_test_src_get_property;
  gobject_class->dispose = gst_gl_test_src_dispose;

  g_object_class_install_property (gobject_class, PROP_PATTERN,
      g_param_spec_enum ("pattern", "Pattern",
          "Type of test pattern to generate", GST_TYPE_GL_TEST_SRC_PATTERN,
          GST_GL_TEST_SRC_SMPTE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class,
      PROP_TIMESTAMP_OFFSET, g_param_spec_int64 ("timestamp-offset",
          "Timestamp offset",
          "An offset added to timestamps set on buffers (in ns)", G_MININT64,
          G_MAXINT64, 0, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_IS_LIVE,
      g_param_spec_boolean ("is-live", "Is Live",
          "Whether to act as a live source", FALSE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_set_metadata (element_class, "Video test source",
      "Source/Video", "Creates a test video stream",
      "David A. Schleef <ds@schleef.org>");

  gst_element_class_add_pad_template (element_class,
      gst_static_pad_template_get (&src_factory));

  element_class->set_context = gst_gl_test_src_set_context;

  gstbasesrc_class->set_caps = gst_gl_test_src_setcaps;
  gstbasesrc_class->is_seekable = gst_gl_test_src_is_seekable;
  gstbasesrc_class->do_seek = gst_gl_test_src_do_seek;
  gstbasesrc_class->query = gst_gl_test_src_query;
  gstbasesrc_class->get_times = gst_gl_test_src_get_times;
  gstbasesrc_class->start = gst_gl_test_src_start;
  gstbasesrc_class->stop = gst_gl_test_src_stop;
  gstbasesrc_class->fixate = gst_gl_test_src_fixate;
  gstbasesrc_class->decide_allocation = gst_gl_test_src_decide_allocation;

  gstpushsrc_class->fill = gst_gl_test_src_fill;
}

static void
gst_gl_test_src_init (GstGLTestSrc * src)
{
  gst_gl_test_src_set_pattern (src, GST_GL_TEST_SRC_SMPTE);

  src->timestamp_offset = 0;

  /* we operate in time */
  gst_base_src_set_format (GST_BASE_SRC (src), GST_FORMAT_TIME);
  gst_base_src_set_live (GST_BASE_SRC (src), FALSE);
}

static GstCaps *
gst_gl_test_src_fixate (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstStructure *structure;

  GST_DEBUG ("fixate");

  caps = gst_caps_make_writable (caps);

  structure = gst_caps_get_structure (caps, 0);

  gst_structure_fixate_field_nearest_int (structure, "width", 320);
  gst_structure_fixate_field_nearest_int (structure, "height", 240);
  gst_structure_fixate_field_nearest_fraction (structure, "framerate", 30, 1);

  caps = GST_BASE_SRC_CLASS (parent_class)->fixate (bsrc, caps);

  return caps;
}

const gchar *snow_vertex_src = "attribute vec4 position; \
    attribute vec2 uv; \
    uniform mat4 mvp; \
    varying vec2 out_uv; \
    void main() \
    { \
       gl_Position = mvp * position; \
       out_uv = uv; \
    }";

const gchar *snow_fragment_src = "uniform float time; \
    varying vec2 out_uv; \
    \
    float rand(vec2 co){ \
        return fract(sin(dot(co.xy, vec2(12.9898,78.233))) * 43758.5453); \
    } \
    void main() \
    { \
      gl_FragColor = rand(time * out_uv) * vec4(1); \
    }";

const gchar *mandelbrot_vertex_src = "attribute vec4 position; \
    attribute vec2 uv; \
    uniform mat4 mvp; \
    uniform float aspect_ratio; \
    varying vec2 fractal_position; \
    \
    void main() \
    { \
       gl_Position = mvp * position; \
       fractal_position = vec2(uv.y - 0.8, aspect_ratio * (uv.x - 0.5)); \
       fractal_position *= 2.5; \
    }";

const gchar *mandelbrot_fragment_src = "uniform float time; \
    varying vec2 fractal_position; \
    \
    const vec4 K = vec4(1.0, 0.66, 0.33, 3.0); \
    \
    vec4 hsv_to_rgb(float hue, float saturation, float value) { \
      vec4 p = abs(fract(vec4(hue) + K) * 6.0 - K.wwww); \
      return value * mix(K.xxxx, clamp(p - K.xxxx, 0.0, 1.0), saturation); \
    } \
    \
    vec4 i_to_rgb(int i) { \
      float hue = float(i) / 100.0 + sin(time); \
      return hsv_to_rgb(hue, 0.5, 0.8); \
    } \
    \
    vec2 pow_2_complex(vec2 c) { \
      return vec2(c.x*c.x - c.y*c.y, 2.0 * c.x * c.y); \
    } \
    \
    vec2 mandelbrot(vec2 c, vec2 c0) { \
      return pow_2_complex(c) + c0; \
    } \
    \
    vec4 iterate_pixel(vec2 position) { \
      vec2 c = vec2(0); \
      for (int i=0; i < 100; i++) { \
        if (c.x*c.x + c.y*c.y > 2.0*2.0) \
          return i_to_rgb(i); \
        c = mandelbrot(c, position); \
      } \
      return vec4(0, 0, 0, 1); \
    } \
    \
    void main() { \
      gl_FragColor = iterate_pixel(fractal_position); \
    }";


const gchar *checkers_vertex_src = "attribute vec4 position; \
    uniform mat4 mvp; \
    void main() \
    { \
       gl_Position = mvp * position; \
    }";

const gchar *checkers_fragment_src = "uniform float checker_width; \
    void main() \
    { \
      vec2 xy_index= floor((gl_FragCoord.xy-vec2(0.5,0.5))/checker_width); \
      vec2 xy_mod=mod(xy_index,vec2(2.0,2.0)); \
      float result=mod(xy_mod.x+xy_mod.y,2.0); \
      gl_FragColor.r=step(result,0.5); \
      gl_FragColor.g=1.0-gl_FragColor.r; \
      gl_FragColor.ba=vec2(0,1); \
    }";


static void
gst_gl_test_src_set_pattern (GstGLTestSrc * gltestsrc, gint pattern_type)
{
  gltestsrc->pattern_type = pattern_type;

  GST_DEBUG_OBJECT (gltestsrc, "setting pattern to %d", pattern_type);

  switch (pattern_type) {
    case GST_GL_TEST_SRC_SMPTE:
      gltestsrc->make_image = gst_gl_test_src_smpte;
      break;
    case GST_GL_TEST_SRC_SNOW:
      gltestsrc->vertex_src = snow_vertex_src;
      gltestsrc->fragment_src = snow_fragment_src;
      gltestsrc->make_image = gst_gl_test_src_shader;
      break;
    case GST_GL_TEST_SRC_BLACK:
      gltestsrc->make_image = gst_gl_test_src_black;
      break;
    case GST_GL_TEST_SRC_WHITE:
      gltestsrc->make_image = gst_gl_test_src_white;
      break;
    case GST_GL_TEST_SRC_RED:
      gltestsrc->make_image = gst_gl_test_src_red;
      break;
    case GST_GL_TEST_SRC_GREEN:
      gltestsrc->make_image = gst_gl_test_src_green;
      break;
    case GST_GL_TEST_SRC_BLUE:
      gltestsrc->make_image = gst_gl_test_src_blue;
      break;
    case GST_GL_TEST_SRC_CHECKERS1:
      gltestsrc->vertex_src = checkers_vertex_src;
      gltestsrc->fragment_src = checkers_fragment_src;
      gltestsrc->make_image = gst_gl_test_src_checkers1;
      break;
    case GST_GL_TEST_SRC_CHECKERS2:
      gltestsrc->vertex_src = checkers_vertex_src;
      gltestsrc->fragment_src = checkers_fragment_src;
      gltestsrc->make_image = gst_gl_test_src_checkers2;
      break;
    case GST_GL_TEST_SRC_CHECKERS4:
      gltestsrc->vertex_src = checkers_vertex_src;
      gltestsrc->fragment_src = checkers_fragment_src;
      gltestsrc->make_image = gst_gl_test_src_checkers4;
      break;
    case GST_GL_TEST_SRC_CHECKERS8:
      gltestsrc->vertex_src = checkers_vertex_src;
      gltestsrc->fragment_src = checkers_fragment_src;
      gltestsrc->make_image = gst_gl_test_src_checkers8;
      break;
    case GST_GL_TEST_SRC_CIRCULAR:
      gltestsrc->make_image = gst_gl_test_src_circular;
      break;
    case GST_GL_TEST_SRC_BLINK:
      gltestsrc->make_image = gst_gl_test_src_black;
      break;
    case GST_GL_TEST_SRC_MANDELBROT:
      gltestsrc->vertex_src = mandelbrot_vertex_src;
      gltestsrc->fragment_src = mandelbrot_fragment_src;
      gltestsrc->make_image = gst_gl_test_src_shader;
      break;
    default:
      g_assert_not_reached ();
  }
}

static void
gst_gl_test_src_dispose (GObject * object)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (object);

  if (src->other_context)
    gst_object_unref (src->other_context);
  src->other_context = NULL;

  G_OBJECT_CLASS (parent_class)->dispose (object);
}

static void
gst_gl_test_src_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (object);

  switch (prop_id) {
    case PROP_PATTERN:
      gst_gl_test_src_set_pattern (src, g_value_get_enum (value));
      break;
    case PROP_TIMESTAMP_OFFSET:
      src->timestamp_offset = g_value_get_int64 (value);
      break;
    case PROP_IS_LIVE:
      gst_base_src_set_live (GST_BASE_SRC (src), g_value_get_boolean (value));
      break;
    default:
      break;
  }
}

static void
gst_gl_test_src_get_property (GObject * object, guint prop_id,
    GValue * value, GParamSpec * pspec)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (object);

  switch (prop_id) {
    case PROP_PATTERN:
      g_value_set_enum (value, src->pattern_type);
      break;
    case PROP_TIMESTAMP_OFFSET:
      g_value_set_int64 (value, src->timestamp_offset);
      break;
    case PROP_IS_LIVE:
      g_value_set_boolean (value, gst_base_src_is_live (GST_BASE_SRC (src)));
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

static gboolean
gst_gl_test_src_setcaps (GstBaseSrc * bsrc, GstCaps * caps)
{
  GstGLTestSrc *gltestsrc = GST_GL_TEST_SRC (bsrc);

  GST_DEBUG ("setcaps");

  if (!gst_video_info_from_caps (&gltestsrc->out_info, caps))
    goto wrong_caps;

  gltestsrc->negotiated = TRUE;

  gst_caps_replace (&gltestsrc->out_caps, caps);

  return TRUE;

/* ERRORS */
wrong_caps:
  {
    GST_WARNING ("wrong caps");
    return FALSE;
  }
}

static void
gst_gl_test_src_set_context (GstElement * element, GstContext * context)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (element);

  gst_gl_handle_set_context (element, context, &src->display,
      &src->other_context);

  if (src->display)
    gst_gl_display_filter_gl_api (src->display, SUPPORTED_GL_APIS);
}

static gboolean
gst_gl_test_src_query (GstBaseSrc * bsrc, GstQuery * query)
{
  gboolean res = FALSE;
  GstGLTestSrc *src;

  src = GST_GL_TEST_SRC (bsrc);

  switch (GST_QUERY_TYPE (query)) {
    case GST_QUERY_CONTEXT:
    {
      res = gst_gl_handle_context_query ((GstElement *) src, query,
          &src->display, &src->other_context);
      if (src->display)
        gst_gl_display_filter_gl_api (src->display, SUPPORTED_GL_APIS);
      break;
    }
    case GST_QUERY_CONVERT:
    {
      GstFormat src_fmt, dest_fmt;
      gint64 src_val, dest_val;

      gst_query_parse_convert (query, &src_fmt, &src_val, &dest_fmt, &dest_val);
      res =
          gst_video_info_convert (&src->out_info, src_fmt, src_val, dest_fmt,
          &dest_val);
      gst_query_set_convert (query, src_fmt, src_val, dest_fmt, dest_val);
      break;
    }
    default:
      res = GST_BASE_SRC_CLASS (parent_class)->query (bsrc, query);
  }

  return res;
}

static void
gst_gl_test_src_get_times (GstBaseSrc * basesrc, GstBuffer * buffer,
    GstClockTime * start, GstClockTime * end)
{
  /* for live sources, sync on the timestamp of the buffer */
  if (gst_base_src_is_live (basesrc)) {
    GstClockTime timestamp = GST_BUFFER_TIMESTAMP (buffer);

    if (GST_CLOCK_TIME_IS_VALID (timestamp)) {
      /* get duration to calculate end time */
      GstClockTime duration = GST_BUFFER_DURATION (buffer);

      if (GST_CLOCK_TIME_IS_VALID (duration))
        *end = timestamp + duration;
      *start = timestamp;
    }
  } else {
    *start = -1;
    *end = -1;
  }
}

static gboolean
gst_gl_test_src_do_seek (GstBaseSrc * bsrc, GstSegment * segment)
{
  GstClockTime time;
  GstGLTestSrc *src;

  src = GST_GL_TEST_SRC (bsrc);

  segment->time = segment->start;
  time = segment->position;

  /* now move to the time indicated */
  if (src->out_info.fps_n) {
    src->n_frames = gst_util_uint64_scale (time,
        src->out_info.fps_n, src->out_info.fps_d * GST_SECOND);
  } else
    src->n_frames = 0;

  if (src->out_info.fps_n) {
    src->running_time = gst_util_uint64_scale (src->n_frames,
        src->out_info.fps_d * GST_SECOND, src->out_info.fps_n);
  } else {
    /* FIXME : Not sure what to set here */
    src->running_time = 0;
  }

  g_return_val_if_fail (src->running_time <= time, FALSE);

  return TRUE;
}

static gboolean
gst_gl_test_src_is_seekable (GstBaseSrc * psrc)
{
  /* we're seekable... */
  return TRUE;
}

static gboolean
gst_gl_test_src_init_shader (GstGLTestSrc * gltestsrc)
{
  if (gst_gl_context_get_gl_api (gltestsrc->context)) {
    /* blocking call, wait until the opengl thread has compiled the shader */
    if (gltestsrc->vertex_src == NULL)
      return FALSE;
    return gst_gl_context_gen_shader (gltestsrc->context, gltestsrc->vertex_src,
        gltestsrc->fragment_src, &gltestsrc->shader);
  }
  return TRUE;
}

static GstFlowReturn
gst_gl_test_src_fill (GstPushSrc * psrc, GstBuffer * buffer)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (psrc);
  GstClockTime next_time;
  gint width, height;
  GstVideoFrame out_frame;
  GstGLSyncMeta *sync_meta;
  guint out_tex;
  gboolean to_download =
      gst_caps_features_is_equal (GST_CAPS_FEATURES_MEMORY_SYSTEM_MEMORY,
      gst_caps_get_features (src->out_caps, 0));
  GstMapFlags out_map_flags = GST_MAP_WRITE;

  to_download |= !gst_is_gl_memory (gst_buffer_peek_memory (buffer, 0));

  if (!to_download)
    out_map_flags |= GST_MAP_GL;

  if (G_UNLIKELY (!src->negotiated || !src->context))
    goto not_negotiated;

  width = GST_VIDEO_INFO_WIDTH (&src->out_info);
  height = GST_VIDEO_INFO_HEIGHT (&src->out_info);

  /* 0 framerate and we are at the second frame, eos */
  if (G_UNLIKELY (GST_VIDEO_INFO_FPS_N (&src->out_info) == 0
          && src->n_frames == 1))
    goto eos;

  if (src->pattern_type == GST_GL_TEST_SRC_BLINK) {
    if (src->n_frames & 0x1)
      src->make_image = gst_gl_test_src_white;
    else
      src->make_image = gst_gl_test_src_black;
  }

  if (!gst_video_frame_map (&out_frame, &src->out_info, buffer, out_map_flags)) {
    return GST_FLOW_NOT_NEGOTIATED;
  }

  if (!to_download) {
    out_tex = *(guint *) out_frame.data[0];
  } else {
    GST_INFO ("Output Buffer does not contain correct meta, "
        "attempting to wrap for download");

    if (!src->download)
      src->download = gst_gl_download_new (src->context);

    gst_gl_download_set_format (src->download, &out_frame.info);

    if (!src->out_tex_id) {
      gst_gl_context_gen_texture (src->context, &src->out_tex_id,
          GST_VIDEO_FORMAT_RGBA, GST_VIDEO_FRAME_WIDTH (&out_frame),
          GST_VIDEO_FRAME_HEIGHT (&out_frame));
    }
    out_tex = src->out_tex_id;
  }

  gst_buffer_replace (&src->buffer, buffer);

  //blocking call, generate a FBO
  if (!gst_gl_context_use_fbo_v2 (src->context, width, height, src->fbo,
          src->depthbuffer, out_tex, gst_gl_test_src_callback,
          (gpointer) src)) {
    goto not_negotiated;
  }

  if (to_download) {
    if (!gst_gl_download_perform_with_data (src->download, out_tex,
            out_frame.data)) {
      GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, ("%s",
              "Failed to init upload format"), (NULL));
      return FALSE;
    }
  }
  gst_video_frame_unmap (&out_frame);

  sync_meta = gst_buffer_get_gl_sync_meta (buffer);
  if (sync_meta)
    gst_gl_sync_meta_set_sync_point (sync_meta, src->context);

  GST_BUFFER_TIMESTAMP (buffer) = src->timestamp_offset + src->running_time;
  GST_BUFFER_OFFSET (buffer) = src->n_frames;
  src->n_frames++;
  GST_BUFFER_OFFSET_END (buffer) = src->n_frames;
  if (src->out_info.fps_n) {
    next_time = gst_util_uint64_scale_int (src->n_frames * GST_SECOND,
        src->out_info.fps_d, src->out_info.fps_n);
    GST_BUFFER_DURATION (buffer) = next_time - src->running_time;
  } else {
    next_time = src->timestamp_offset;
    /* NONE means forever */
    GST_BUFFER_DURATION (buffer) = GST_CLOCK_TIME_NONE;
  }

  src->running_time = next_time;

  return GST_FLOW_OK;

not_negotiated:
  {
    GST_ELEMENT_ERROR (src, CORE, NEGOTIATION, (NULL),
        (_("format wasn't negotiated before get function")));
    return GST_FLOW_NOT_NEGOTIATED;
  }
eos:
  {
    GST_DEBUG_OBJECT (src, "eos: 0 framerate, frame %d", (gint) src->n_frames);
    return GST_FLOW_EOS;
  }
}

static gboolean
gst_gl_test_src_start (GstBaseSrc * basesrc)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (basesrc);

  if (!gst_gl_ensure_element_data (src, &src->display, &src->other_context))
    return FALSE;

  gst_gl_display_filter_gl_api (src->display, SUPPORTED_GL_APIS);

  src->running_time = 0;
  src->n_frames = 0;
  src->negotiated = FALSE;

  return TRUE;
}

static gboolean
gst_gl_test_src_stop (GstBaseSrc * basesrc)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (basesrc);

  gst_caps_replace (&src->out_caps, NULL);

  if (src->context) {
    if (src->shader) {
      gst_object_unref (src->shader);
      src->shader = NULL;
    }

    if (src->out_tex_id) {
      gst_gl_context_del_texture (src->context, &src->out_tex_id);
    }

    if (src->download) {
      gst_object_unref (src->download);
      src->download = NULL;
    }
    //blocking call, delete the FBO
    gst_gl_context_del_fbo (src->context, src->fbo, src->depthbuffer);
    gst_object_unref (src->context);
    src->context = NULL;
  }

  if (src->display) {
    gst_object_unref (src->display);
    src->display = NULL;
  }

  return TRUE;
}

static gboolean
gst_gl_test_src_decide_allocation (GstBaseSrc * basesrc, GstQuery * query)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (basesrc);
  GstBufferPool *pool = NULL;
  GstStructure *config;
  GstCaps *caps;
  guint min, max, size;
  gboolean update_pool;
  GError *error = NULL;
  guint idx;
  guint out_width, out_height;
  GstGLContext *other_context = NULL;
  gboolean same_downstream_gl_context = FALSE;

  if (!gst_gl_ensure_element_data (src, &src->display, &src->other_context))
    return FALSE;

  gst_gl_display_filter_gl_api (src->display, SUPPORTED_GL_APIS);

  if (gst_query_find_allocation_meta (query,
          GST_VIDEO_GL_TEXTURE_UPLOAD_META_API_TYPE, &idx)) {
    GstGLContext *context;
    const GstStructure *upload_meta_params;
    gpointer handle;
    gchar *type;
    gchar *apis;

    gst_query_parse_nth_allocation_meta (query, idx, &upload_meta_params);
    if (upload_meta_params) {
      if (gst_structure_get (upload_meta_params, "gst.gl.GstGLContext",
              GST_GL_TYPE_CONTEXT, &context, NULL) && context) {
        GstGLContext *old = src->context;

        src->context = context;
        if (old)
          gst_object_unref (old);
        same_downstream_gl_context = TRUE;
      } else if (gst_structure_get (upload_meta_params, "gst.gl.context.handle",
              G_TYPE_POINTER, &handle, "gst.gl.context.type", G_TYPE_STRING,
              &type, "gst.gl.context.apis", G_TYPE_STRING, &apis, NULL)
          && handle) {
        GstGLPlatform platform = GST_GL_PLATFORM_NONE;
        GstGLAPI gl_apis;

        GST_DEBUG ("got GL context handle 0x%p with type %s and apis %s",
            handle, type, apis);

        platform = gst_gl_platform_from_string (type);
        gl_apis = gst_gl_api_from_string (apis);

        if (gl_apis && platform)
          other_context =
              gst_gl_context_new_wrapped (src->display, (guintptr) handle,
              platform, gl_apis);
      }
    }
  }

  if (src->other_context) {
    if (!other_context) {
      other_context = src->other_context;
    } else {
      GST_ELEMENT_WARNING (src, LIBRARY, SETTINGS,
          ("%s", "Cannot share with more than one GL context"),
          ("%s", "Cannot share with more than one GL context"));
    }
  }

  if (!src->context) {
    src->context = gst_gl_context_new (src->display);
    if (!gst_gl_context_create (src->context, other_context, &error))
      goto context_error;
  }

  out_width = GST_VIDEO_INFO_WIDTH (&src->out_info);
  out_height = GST_VIDEO_INFO_HEIGHT (&src->out_info);

  if (!gst_gl_context_gen_fbo (src->context, out_width, out_height,
          &src->fbo, &src->depthbuffer))
    goto context_error;

  gst_query_parse_allocation (query, &caps, NULL);

  if (gst_query_get_n_allocation_pools (query) > 0) {
    gst_query_parse_nth_allocation_pool (query, 0, &pool, &size, &min, &max);

    update_pool = TRUE;
  } else {
    GstVideoInfo vinfo;

    gst_video_info_init (&vinfo);
    gst_video_info_from_caps (&vinfo, caps);
    size = vinfo.size;
    min = max = 0;
    update_pool = FALSE;
  }

  if (!pool || (!same_downstream_gl_context
          && gst_query_find_allocation_meta (query, GST_GL_SYNC_META_API_TYPE,
              NULL)
          && !gst_buffer_pool_has_option (pool,
              GST_BUFFER_POOL_OPTION_GL_SYNC_META))) {
    /* can't use this pool */
    if (pool)
      gst_object_unref (pool);
    pool = gst_gl_buffer_pool_new (src->context);
  }
  config = gst_buffer_pool_get_config (pool);

  gst_buffer_pool_config_set_params (config, caps, size, min, max);
  gst_buffer_pool_config_add_option (config, GST_BUFFER_POOL_OPTION_VIDEO_META);
  if (gst_query_find_allocation_meta (query, GST_GL_SYNC_META_API_TYPE, NULL))
    gst_buffer_pool_config_add_option (config,
        GST_BUFFER_POOL_OPTION_GL_SYNC_META);
  gst_buffer_pool_config_add_option (config,
      GST_BUFFER_POOL_OPTION_VIDEO_GL_TEXTURE_UPLOAD_META);

  gst_buffer_pool_set_config (pool, config);

  if (update_pool)
    gst_query_set_nth_allocation_pool (query, 0, pool, size, min, max);
  else
    gst_query_add_allocation_pool (query, pool, size, min, max);

  gst_gl_test_src_init_shader (src);

  gst_object_unref (pool);

  return TRUE;

context_error:
  {
    GST_ELEMENT_ERROR (src, RESOURCE, NOT_FOUND, ("%s", error->message),
        (NULL));
    gst_object_unref (src->context);
    src->context = NULL;
    return FALSE;
  }
}

//opengl scene
static void
gst_gl_test_src_callback (gpointer stuff)
{
  GstGLTestSrc *src = GST_GL_TEST_SRC (stuff);

  src->make_image (src, src->buffer, GST_VIDEO_INFO_WIDTH (&src->out_info),
      GST_VIDEO_INFO_HEIGHT (&src->out_info));

  gst_buffer_unref (src->buffer);
  src->buffer = NULL;
}
