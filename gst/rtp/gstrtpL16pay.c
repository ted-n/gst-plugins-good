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

#include <string.h>

#include <gst/audio/audio.h>
#include <gst/rtp/gstrtpbuffer.h>

#include "gstrtpL16pay.h"
#include "gstrtpchannels.h"

GST_DEBUG_CATEGORY_STATIC (rtpL16pay_debug);
#define GST_CAT_DEFAULT (rtpL16pay_debug)

static GstStaticPadTemplate gst_rtp_L16_pay_sink_template =
GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("audio/x-raw, "
        "format = (string) S16BE, "
        "layout = (string) interleaved, "
        "rate = (int) [ 1, MAX ], " "channels = (int) [ 1, MAX ]")
    );

static GstStaticPadTemplate gst_rtp_L16_pay_src_template =
    GST_STATIC_PAD_TEMPLATE ("src",
    GST_PAD_SRC,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS ("application/x-rtp, "
        "media = (string) \"audio\", "
        "payload = (int) [ 96, 127 ], "
        "clock-rate = (int) [ 1, MAX ], "
        "encoding-name = (string) \"L16\", "
        "channels = (int) [ 1, MAX ];"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "encoding-name = (string) \"L16\", "
        "payload = (int) " GST_RTP_PAYLOAD_L16_STEREO_STRING ", "
        "clock-rate = (int) 44100;"
        "application/x-rtp, "
        "media = (string) \"audio\", "
        "encoding-name = (string) \"L16\", "
        "payload = (int) " GST_RTP_PAYLOAD_L16_MONO_STRING ", "
        "clock-rate = (int) 44100")
    );

static gboolean gst_rtp_L16_pay_setcaps (GstRTPBasePayload * basepayload,
    GstCaps * caps);
static GstCaps *gst_rtp_L16_pay_getcaps (GstRTPBasePayload * rtppayload,
    GstPad * pad, GstCaps * filter);
static GstFlowReturn
gst_rtp_L16_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer);

#define gst_rtp_L16_pay_parent_class parent_class
G_DEFINE_TYPE (GstRtpL16Pay, gst_rtp_L16_pay, GST_TYPE_RTP_BASE_AUDIO_PAYLOAD);

static void
gst_rtp_L16_pay_class_init (GstRtpL16PayClass * klass)
{
  GstElementClass *gstelement_class;
  GstRTPBasePayloadClass *gstrtpbasepayload_class;

  gstelement_class = (GstElementClass *) klass;
  gstrtpbasepayload_class = (GstRTPBasePayloadClass *) klass;

  gstrtpbasepayload_class->set_caps = gst_rtp_L16_pay_setcaps;
  gstrtpbasepayload_class->get_caps = gst_rtp_L16_pay_getcaps;
  gstrtpbasepayload_class->handle_buffer = gst_rtp_L16_pay_handle_buffer;

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_L16_pay_src_template));
  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&gst_rtp_L16_pay_sink_template));

  gst_element_class_set_static_metadata (gstelement_class,
      "RTP audio payloader", "Codec/Payloader/Network/RTP",
      "Payload-encode Raw audio into RTP packets (RFC 3551)",
      "Wim Taymans <wim.taymans@gmail.com>");

  GST_DEBUG_CATEGORY_INIT (rtpL16pay_debug, "rtpL16pay", 0,
      "L16 RTP Payloader");
}

static void
gst_rtp_L16_pay_init (GstRtpL16Pay * rtpL16pay)
{
  GstRTPBaseAudioPayload *rtpbaseaudiopayload;

  rtpbaseaudiopayload = GST_RTP_BASE_AUDIO_PAYLOAD (rtpL16pay);

  /* tell rtpbaseaudiopayload that this is a sample based codec */
  gst_rtp_base_audio_payload_set_sample_based (rtpbaseaudiopayload);
}

static gboolean
gst_rtp_L16_pay_setcaps (GstRTPBasePayload * basepayload, GstCaps * caps)
{
  GstRtpL16Pay *rtpL16pay;
  gboolean res;
  gchar *params;
  GstAudioInfo *info;
  const GstRTPChannelOrder *order;
  GstRTPBaseAudioPayload *rtpbaseaudiopayload;

  rtpbaseaudiopayload = GST_RTP_BASE_AUDIO_PAYLOAD (basepayload);
  rtpL16pay = GST_RTP_L16_PAY (basepayload);

  info = &rtpL16pay->info;
  gst_audio_info_init (info);
  if (!gst_audio_info_from_caps (info, caps))
    goto invalid_caps;

  order = gst_rtp_channels_get_by_pos (info->channels, info->position);
  rtpL16pay->order = order;

  gst_rtp_base_payload_set_options (basepayload, "audio", TRUE, "L16",
      info->rate);
  params = g_strdup_printf ("%d", info->channels);

  if (!order && info->channels > 2) {
    GST_ELEMENT_WARNING (rtpL16pay, STREAM, DECODE,
        (NULL), ("Unknown channel order for %d channels", info->channels));
  }

  if (order && order->name) {
    res = gst_rtp_base_payload_set_outcaps (basepayload,
        "encoding-params", G_TYPE_STRING, params, "channels", G_TYPE_INT,
        info->channels, "channel-order", G_TYPE_STRING, order->name, NULL);
  } else {
    res = gst_rtp_base_payload_set_outcaps (basepayload,
        "encoding-params", G_TYPE_STRING, params, "channels", G_TYPE_INT,
        info->channels, NULL);
  }

  g_free (params);

  /* octet-per-sample is 2 * channels for L16 */
  gst_rtp_base_audio_payload_set_sample_options (rtpbaseaudiopayload,
      2 * info->channels);

  return res;

  /* ERRORS */
invalid_caps:
  {
    GST_DEBUG_OBJECT (rtpL16pay, "invalid caps");
    return FALSE;
  }
}

static GstCaps *
gst_rtp_L16_pay_getcaps (GstRTPBasePayload * rtppayload, GstPad * pad,
    GstCaps * filter)
{
  GstCaps *otherpadcaps;
  GstCaps *caps;

  caps = gst_pad_get_pad_template_caps (pad);

  otherpadcaps = gst_pad_get_allowed_caps (rtppayload->srcpad);
  if (otherpadcaps) {
    if (!gst_caps_is_empty (otherpadcaps)) {
      GstStructure *structure;
      gint channels;
      gint pt;
      gint rate;

      structure = gst_caps_get_structure (otherpadcaps, 0);
      caps = gst_caps_make_writable (caps);

      if (gst_structure_get_int (structure, "channels", &channels)) {
        gst_caps_set_simple (caps, "channels", G_TYPE_INT, channels, NULL);
      } else if (gst_structure_get_int (structure, "payload", &pt)) {
        if (pt == 10)
          gst_caps_set_simple (caps, "channels", G_TYPE_INT, 2, NULL);
        else if (pt == 11)
          gst_caps_set_simple (caps, "channels", G_TYPE_INT, 1, NULL);
      }

      if (gst_structure_get_int (structure, "clock-rate", &rate)) {
        gst_caps_set_simple (caps, "rate", G_TYPE_INT, rate, NULL);
      } else if (gst_structure_get_int (structure, "payload", &pt)) {
        if (pt == 10 || pt == 11)
          gst_caps_set_simple (caps, "rate", G_TYPE_INT, 44100, NULL);
      }

    }
    gst_caps_unref (otherpadcaps);
  }

  if (filter) {
    GstCaps *tcaps = caps;

    caps = gst_caps_intersect_full (filter, tcaps, GST_CAPS_INTERSECT_FIRST);
    gst_caps_unref (tcaps);
  }

  return caps;
}

static GstFlowReturn
gst_rtp_L16_pay_handle_buffer (GstRTPBasePayload * basepayload,
    GstBuffer * buffer)
{
  GstRtpL16Pay *rtpL16pay;

  rtpL16pay = GST_RTP_L16_PAY (basepayload);
  buffer = gst_buffer_make_writable (buffer);

  if (rtpL16pay->order &&
      !gst_audio_buffer_reorder_channels (buffer, rtpL16pay->info.finfo->format,
          rtpL16pay->info.channels, rtpL16pay->info.position,
          rtpL16pay->order->pos)) {
    return GST_FLOW_ERROR;
  }

  return GST_RTP_BASE_PAYLOAD_CLASS (parent_class)->handle_buffer (basepayload,
      buffer);
}

gboolean
gst_rtp_L16_pay_plugin_init (GstPlugin * plugin)
{
  return gst_element_register (plugin, "rtpL16pay",
      GST_RANK_SECONDARY, GST_TYPE_RTP_L16_PAY);
}
