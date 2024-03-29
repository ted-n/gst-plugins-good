/* GStreamer
 * Copyright (C) <2007> Wim Taymans <wim.taymans@gmail.com>
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

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include <gst/rtp/gstrtpbuffer.h>

#include <string.h>
#include "gstrtpac3depay.h"

GST_DEBUG_CATEGORY_STATIC (rtpac3depay_debug);
#define GST_CAT_DEFAULT (rtpac3depay_debug)

static GstStaticPadTemplate gst_rtp_ac3_depay_src_template =
GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/ac3")
    );

static GstStaticPadTemplate gst_rtp_ac3_depay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "clock-rate = (int) { 32000, 44100, 48000 }, "
        "encoding-name = (string) \"AC3\"")
    );

G_DEFINE_TYPE (GstRtpAC3Depay, gst_rtp_ac3_depay, GST_TYPE_RTP_BASE_DEPAYLOAD);

static gboolean gst_rtp_ac3_depay_setcaps (GstRTPBaseDepayload * depayload,
    GstCaps * caps);
static GstBuffer *gst_rtp_ac3_depay_process (GstRTPBaseDepayload * depayload,
    GstBuffer * buf);

static void
gst_rtp_ac3_depay_class_init (GstRtpAC3DepayClass * klass)
{
  GstElementClass *gstelement_class;
  GstRTPBaseDepayloadClass *gstrtpbasedepayload_class;

  gstelement_class = (GstElementClass *) klass;
  gstrtpbasedepayload_class = (GstRTPBaseDepayloadClass *) klass;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_ac3_depay_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_ac3_depay_sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP AC3 depayloader", "Codec/Depayloader/Network/RTP",
      "Extracts AC3 audio from RTP packets (RFC 4184)",
      "Wim Taymans <wim.taymans@gmail.com>");

  gstrtpbasedepayload_class->set_caps = gst_rtp_ac3_depay_setcaps;
  gstrtpbasedepayload_class->process = gst_rtp_ac3_depay_process;

  GST_DEBUG_CATEGORY_INIT (rtpac3depay_debug, "rtpac3depay", 0,
      "AC3 Audio RTP Depayloader");
}

static void
gst_rtp_ac3_depay_init (GstRtpAC3Depay * rtpac3depay)
{
  /* needed because of G_DEFINE_TYPE */
}

static gboolean
gst_rtp_ac3_depay_setcaps (GstRTPBaseDepayload * depayload, GstCaps * caps)
{
  GstStructure *structure;
  gint clock_rate;
  GstCaps *srccaps;
  gboolean res;

  structure = gst_caps_get_structure (caps, 0);

  if (!gst_structure_get_int (structure, "clock-rate", &clock_rate))
    clock_rate = 90000;         /* default */
  depayload->clock_rate = clock_rate;

  srccaps = gst_caps_new_empty_simple ("audio/ac3");
  res = gst_pad_set_caps (depayload->srcpad, srccaps);
  gst_caps_unref (srccaps);

  return res;
}

struct frmsize_s
{
  guint16 bit_rate;
  guint16 frm_size[3];
};

static const struct frmsize_s frmsizecod_tbl[] = {
  {32, {64, 69, 96}},
  {32, {64, 70, 96}},
  {40, {80, 87, 120}},
  {40, {80, 88, 120}},
  {48, {96, 104, 144}},
  {48, {96, 105, 144}},
  {56, {112, 121, 168}},
  {56, {112, 122, 168}},
  {64, {128, 139, 192}},
  {64, {128, 140, 192}},
  {80, {160, 174, 240}},
  {80, {160, 175, 240}},
  {96, {192, 208, 288}},
  {96, {192, 209, 288}},
  {112, {224, 243, 336}},
  {112, {224, 244, 336}},
  {128, {256, 278, 384}},
  {128, {256, 279, 384}},
  {160, {320, 348, 480}},
  {160, {320, 349, 480}},
  {192, {384, 417, 576}},
  {192, {384, 418, 576}},
  {224, {448, 487, 672}},
  {224, {448, 488, 672}},
  {256, {512, 557, 768}},
  {256, {512, 558, 768}},
  {320, {640, 696, 960}},
  {320, {640, 697, 960}},
  {384, {768, 835, 1152}},
  {384, {768, 836, 1152}},
  {448, {896, 975, 1344}},
  {448, {896, 976, 1344}},
  {512, {1024, 1114, 1536}},
  {512, {1024, 1115, 1536}},
  {576, {1152, 1253, 1728}},
  {576, {1152, 1254, 1728}},
  {640, {1280, 1393, 1920}},
  {640, {1280, 1394, 1920}}
};

static GstBuffer *
gst_rtp_ac3_depay_process (GstRTPBaseDepayload * depayload, GstBuffer * buf)
{
  GstRtpAC3Depay *rtpac3depay;
  GstBuffer *outbuf;
  GstRTPBuffer rtp = { NULL, };
  guint8 *payload;
  guint16 FT, NF;

  rtpac3depay = GST_RTP_AC3_DEPAY (depayload);

  gst_rtp_buffer_map (buf, GST_MAP_READ, &rtp);

  if (gst_rtp_buffer_get_payload_len (&rtp) < 2)
    goto empty_packet;

  payload = gst_rtp_buffer_get_payload (&rtp);

  /* strip off header
   *
   *  0                   1
   *  0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   * |    MBZ    | FT|       NF      |
   * +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
   */
  FT = payload[0] & 0x3;
  NF = payload[1];

  GST_DEBUG_OBJECT (rtpac3depay, "FT: %d, NF: %d", FT, NF);

  /* We don't bother with fragmented packets yet */
  outbuf = gst_rtp_buffer_get_payload_subbuffer (&rtp, 2, -1);

  gst_rtp_buffer_unmap (&rtp);

  if (outbuf)
    GST_DEBUG_OBJECT (rtpac3depay, "pushing buffer of size %" G_GSIZE_FORMAT,
        gst_buffer_get_size (outbuf));

  return outbuf;

  /* ERRORS */
empty_packet:
  {
    GST_ELEMENT_WARNING (rtpac3depay, STREAM, DECODE,
        ("Empty Payload."), (NULL));
    gst_rtp_buffer_unmap (&rtp);
    return NULL;
  }
}

gboolean
gst_rtp_ac3_depay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpac3depay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_AC3_DEPAY);
}
