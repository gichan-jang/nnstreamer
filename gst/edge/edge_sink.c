/* SPDX-License-Identifier: LGPL-2.1-only */
/**
 * Copyright (C) 2022 Samsung Electronics Co., Ltd.
 *
 * @file    edge_sink.c
 * @date    01 Aug 2022
 * @brief   Publish incoming streams
 * @author  Yechan Choi <yechan9.choi@samsung.com>
 * @see     http://github.com/nnstreamer/nnstreamer
 * @bug     No known bugs
 *
 */
#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include "edge_sink.h"

GST_DEBUG_CATEGORY_STATIC (gst_edgesink_debug);
#define GST_CAT_DEFAULT gst_edgesink_debug

/**
 * @brief the capabilities of the inputs.
 */
static GstStaticPadTemplate sinktemplate = GST_STATIC_PAD_TEMPLATE ("sink",
    GST_PAD_SINK,
    GST_PAD_ALWAYS,
    GST_STATIC_CAPS_ANY);

/**
 * @brief edgesink properties
 */
enum
{
  PROP_0,

  PROP_HOST,
  PROP_PORT,
  PROP_DEST_HOST,
  PROP_DEST_PORT,
  PROP_CONNECT_TYPE,
  PROP_TOPIC,
  PROP_WAIT_CONNECTION,
  PROP_CONNECTION_TIMEOUT,
  PROP_CUSTOM_LIB,

  PROP_LAST
};
#define DEFAULT_MQTT_HOST "127.0.0.1"
#define DEFAULT_MQTT_PORT 1883

#define gst_edgesink_parent_class parent_class
G_DEFINE_TYPE (GstEdgeSink, gst_edgesink, GST_TYPE_BASE_SINK);

static void gst_edgesink_set_property (GObject * object,
    guint prop_id, const GValue * value, GParamSpec * pspec);

static void gst_edgesink_get_property (GObject * object,
    guint prop_id, GValue * value, GParamSpec * pspec);

static void gst_edgesink_finalize (GObject * object);

static gboolean gst_edgesink_start (GstBaseSink * basesink);
static gboolean gst_edgesink_stop (GstBaseSink * basesink);
static GstFlowReturn gst_edgesink_render (GstBaseSink * basesink,
    GstBuffer * buffer);
static gboolean gst_edgesink_set_caps (GstBaseSink * basesink, GstCaps * caps);

static gchar *gst_edgesink_get_host (GstEdgeSink * self);
static void gst_edgesink_set_host (GstEdgeSink * self, const gchar * host);

static guint16 gst_edgesink_get_port (GstEdgeSink * self);
static void gst_edgesink_set_port (GstEdgeSink * self, const guint16 port);

static nns_edge_connect_type_e gst_edgesink_get_connect_type (GstEdgeSink *
    self);
static void gst_edgesink_set_connect_type (GstEdgeSink * self,
    const nns_edge_connect_type_e connect_type);

/**
 * @brief initialize the class
 */
static void
gst_edgesink_class_init (GstEdgeSinkClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  GstElementClass *gstelement_class = GST_ELEMENT_CLASS (klass);
  GstBaseSinkClass *gstbasesink_class = GST_BASE_SINK_CLASS (klass);

  gobject_class->set_property = gst_edgesink_set_property;
  gobject_class->get_property = gst_edgesink_get_property;
  gobject_class->finalize = gst_edgesink_finalize;

  g_object_class_install_property (gobject_class, PROP_HOST,
      g_param_spec_string ("host", "Host",
          "A self host address to accept connection from edgesrc", DEFAULT_HOST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_PORT,
      g_param_spec_uint ("port", "Port",
          "A self port address to accept connection from edgesrc. "
          "If the port is set to 0 then the available port is allocated. ",
          0, 65535, DEFAULT_PORT, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CONNECT_TYPE,
      g_param_spec_enum ("connect-type", "Connect Type",
          "The connections type between edgesink and edgesrc.",
          GST_TYPE_EDGE_CONNECT_TYPE, DEFAULT_CONNECT_TYPE,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEST_HOST,
      g_param_spec_string ("dest-host", "Destination Host",
          "The destination hostname of the broker", DEFAULT_MQTT_HOST,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_DEST_PORT,
      g_param_spec_uint ("dest-port", "Destination Port",
          "The destination port of the broker", 0,
          65535, DEFAULT_MQTT_PORT,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_TOPIC,
      g_param_spec_string ("topic", "Topic",
          "The main topic of the host and option if necessary. "
          "(topic)/(optional topic for main topic).", "",
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_WAIT_CONNECTION,
      g_param_spec_boolean ("wait-connection", "Wait connection to edgesrc",
          "Wait until edgesink is connected to edgesrc. "
          "In case of false(default), the buffers entering the edgesink are dropped.",
          FALSE, G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CONNECTION_TIMEOUT,
      g_param_spec_uint64 ("connection-timeout",
          "Timeout for waiting a connection",
          "The timeout (in milliseconds) for waiting a connection to receiver. "
          "0 timeout (default) means infinite wait.", 0, G_MAXUINT64, 0,
          G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));
  g_object_class_install_property (gobject_class, PROP_CUSTOM_LIB,
      g_param_spec_string ("custom-lib", "Custom connection lib path",
          "User defined custom connection lib path.",
          "", G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS));

  gst_element_class_add_pad_template (gstelement_class,
      gst_static_pad_template_get (&sinktemplate));

  gst_element_class_set_static_metadata (gstelement_class,
      "EdgeSink", "Sink/Edge",
      "Publish incoming streams", "Samsung Electronics Co., Ltd.");

  gstbasesink_class->start = gst_edgesink_start;
  gstbasesink_class->stop = gst_edgesink_stop;
  gstbasesink_class->render = gst_edgesink_render;
  gstbasesink_class->set_caps = gst_edgesink_set_caps;

  GST_DEBUG_CATEGORY_INIT (GST_CAT_DEFAULT,
      GST_EDGE_ELEM_NAME_SINK, 0, "Edge sink");
}

/**
 * @brief initialize the new element
 */
static void
gst_edgesink_init (GstEdgeSink * self)
{
  self->host = g_strdup (DEFAULT_HOST);
  self->port = DEFAULT_PORT;
  self->dest_host = g_strdup (DEFAULT_HOST);
  self->dest_port = DEFAULT_PORT;
  self->topic = NULL;
  self->connect_type = DEFAULT_CONNECT_TYPE;
  self->wait_connection = FALSE;
  self->connection_timeout = 0;
  self->custom_lib = NULL;
  self->is_connected = FALSE;
  g_mutex_init (&self->lock);
  g_cond_init (&self->cond);
}

/**
 * @brief set property
 */
static void
gst_edgesink_set_property (GObject * object, guint prop_id,
    const GValue * value, GParamSpec * pspec)
{
  GstEdgeSink *self = GST_EDGESINK (object);

  switch (prop_id) {
    case PROP_HOST:
      gst_edgesink_set_host (self, g_value_get_string (value));
      break;
    case PROP_PORT:
      gst_edgesink_set_port (self, g_value_get_uint (value));
      break;
    case PROP_DEST_HOST:
      if (!g_value_get_string (value)) {
        nns_logw ("dest host property cannot be NULL.");
        break;
      }
      g_free (self->dest_host);
      self->dest_host = g_value_dup_string (value);
      break;
    case PROP_DEST_PORT:
      self->dest_port = g_value_get_uint (value);
      break;
    case PROP_CONNECT_TYPE:
      gst_edgesink_set_connect_type (self, g_value_get_enum (value));
      break;
    case PROP_TOPIC:
      if (!g_value_get_string (value)) {
        nns_logw ("topic property cannot be NULL. Query-hybrid is disabled.");
        break;
      }
      g_free (self->topic);
      self->topic = g_value_dup_string (value);
      break;
    case PROP_WAIT_CONNECTION:
      self->wait_connection = g_value_get_boolean (value);
      break;
    case PROP_CONNECTION_TIMEOUT:
      self->connection_timeout = g_value_get_uint64 (value);
      break;
    case PROP_CUSTOM_LIB:
      g_free (self->custom_lib);
      self->custom_lib = g_value_dup_string (value);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * @brief get property
 */
static void
gst_edgesink_get_property (GObject * object, guint prop_id, GValue * value,
    GParamSpec * pspec)
{
  GstEdgeSink *self = GST_EDGESINK (object);

  switch (prop_id) {
    case PROP_HOST:
      g_value_set_string (value, gst_edgesink_get_host (self));
      break;
    case PROP_PORT:
      g_value_set_uint (value, gst_edgesink_get_port (self));
      break;
    case PROP_DEST_HOST:
      g_value_set_string (value, self->dest_host);
      break;
    case PROP_DEST_PORT:
      g_value_set_uint (value, self->dest_port);
      break;
    case PROP_CONNECT_TYPE:
      g_value_set_enum (value, gst_edgesink_get_connect_type (self));
      break;
    case PROP_TOPIC:
      g_value_set_string (value, self->topic);
      break;
    case PROP_WAIT_CONNECTION:
      g_value_set_boolean (value, self->wait_connection);
      break;
    case PROP_CONNECTION_TIMEOUT:
      g_value_set_uint64 (value, self->connection_timeout);
      break;
    case PROP_CUSTOM_LIB:
      g_value_set_string (value, self->custom_lib);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
  }
}

/**
 * @brief finalize the object
 */
static void
gst_edgesink_finalize (GObject * object)
{
  GstEdgeSink *self = GST_EDGESINK (object);

  g_free (self->host);
  self->host = NULL;

  g_free (self->dest_host);
  self->dest_host = NULL;

  g_free (self->topic);
  self->topic = NULL;

  g_free (self->custom_lib);
  self->custom_lib = NULL;
  g_mutex_clear (&self->lock);
  g_cond_clear (&self->cond);

  if (self->edge_h) {
    nns_edge_release_handle (self->edge_h);
    self->edge_h = NULL;
  }

  G_OBJECT_CLASS (parent_class)->finalize (object);
}


/**
 * @brief nnstreamer-edge event callback.
 */
static int
_nns_edge_event_cb (nns_edge_event_h event_h, void *user_data)
{
  nns_edge_event_e event_type;
  int ret = NNS_EDGE_ERROR_NONE;

  GstEdgeSink *self = GST_EDGESINK (user_data);
  ret = nns_edge_event_get_type (event_h, &event_type);
  if (NNS_EDGE_ERROR_NONE != ret) {
    nns_loge ("Failed to get event type!");
    return ret;
  }

  switch (event_type) {
    case NNS_EDGE_EVENT_CONNECTION_COMPLETED:
    {
      g_mutex_lock (&self->lock);
      self->is_connected = TRUE;
      g_cond_broadcast (&self->cond);
      g_mutex_unlock (&self->lock);
      break;
    }
    default:
      break;
  }

  return ret;
}

/**
 * @brief start processing of edgesink
 */
static gboolean
gst_edgesink_start (GstBaseSink * basesink)
{
  GstEdgeSink *self = GST_EDGESINK (basesink);

  int ret;
  char *port = NULL;

  if (NNS_EDGE_CONNECT_TYPE_CUSTOM != self->connect_type) {
    ret = nns_edge_create_handle (NULL, self->connect_type,
        NNS_EDGE_NODE_TYPE_PUB, &self->edge_h);
  } else {
    if (!self->custom_lib) {
      nns_loge ("Failed to start edgesink. Custom library is not set.");
      return FALSE;
    }
    ret = nns_edge_custom_create_handle (NULL, self->custom_lib,
        NNS_EDGE_NODE_TYPE_PUB, &self->edge_h);
  }

  if (NNS_EDGE_ERROR_NONE != ret) {
    nns_loge ("Failed to get nnstreamer edge handle.");

    if (self->edge_h) {
      nns_edge_release_handle (self->edge_h);
      self->edge_h = NULL;
    }

    return FALSE;
  }

  if (self->host)
    nns_edge_set_info (self->edge_h, "HOST", self->host);
  if (self->port > 0) {
    port = g_strdup_printf ("%u", self->port);
    nns_edge_set_info (self->edge_h, "PORT", port);
    g_free (port);
  }
  if (self->dest_host)
    nns_edge_set_info (self->edge_h, "DEST_HOST", self->dest_host);
  if (self->dest_port > 0) {
    port = g_strdup_printf ("%u", self->dest_port);
    nns_edge_set_info (self->edge_h, "DEST_PORT", port);
    g_free (port);
  }
  if (self->topic)
    nns_edge_set_info (self->edge_h, "TOPIC", self->topic);

  nns_edge_set_event_callback (self->edge_h, _nns_edge_event_cb, self);

  if (0 != nns_edge_start (self->edge_h)) {
    nns_loge
        ("Failed to start NNStreamer-edge. Please check server IP and port.");
    return FALSE;
  }

  return TRUE;
}

/**
 * @brief If wait-connection is enabled, wait for connection until the connection is established or timeout occurs. Otherwise, return immediately.
 */
static gboolean
_wait_connection (GstEdgeSink *sink)
{
  gint64 end_time;
  gboolean connected;

  if (!sink->wait_connection)
    return TRUE;

  if (0 == sink->connection_timeout) {
    end_time = G_MAXINT64;
  } else {
    end_time = g_get_monotonic_time ()
        + sink->connection_timeout * G_TIME_SPAN_MILLISECOND;
  }

  g_mutex_lock (&sink->lock);
  while (!sink->is_connected) {
    if (!g_cond_wait_until (&sink->cond, &sink->lock, end_time)) {
      nns_loge ("Failed to wait connection.");
      break;
    }
  }
  connected = sink->is_connected;
  g_mutex_unlock (&sink->lock);

  return connected;
}

/**
 * @brief Stop processing of edgesink
 */
static gboolean
gst_edgesink_stop (GstBaseSink * basesink)
{
  GstEdgeSink *self = GST_EDGESINK (basesink);
  int ret;

  ret = nns_edge_stop (self->edge_h);
  if (NNS_EDGE_ERROR_NONE != ret) {
    nns_loge ("Failed to stop edge (error code: %d).", ret);
    return FALSE;
  }

  return TRUE;
}

/**
 * @brief render buffer, send buffer
 */
static GstFlowReturn
gst_edgesink_render (GstBaseSink * basesink, GstBuffer * buffer)
{
  GstEdgeSink *self = GST_EDGESINK (basesink);
  GstCaps *caps;
  GstStructure *structure;
  gboolean is_tensor;
  nns_edge_data_h data_h;
  guint i, num_mems;
  int ret;
  GstMemory *mem[NNS_TENSOR_SIZE_LIMIT];
  GstMapInfo map[NNS_TENSOR_SIZE_LIMIT];

  if (!_wait_connection (self)) {
    nns_loge ("Failed to send buffer.");
    return GST_FLOW_ERROR;
  }

  ret = nns_edge_data_create (&data_h);
  if (ret != NNS_EDGE_ERROR_NONE) {
    nns_loge ("Failed to create data handle in edgesink.");
    return GST_FLOW_ERROR;
  }

  caps = gst_pad_get_current_caps (GST_BASE_SINK_PAD (basesink));
  structure = gst_caps_get_structure (caps, 0);
  is_tensor = gst_structure_is_tensor_stream (structure);
  gst_caps_unref (caps);

  if (is_tensor)
    num_mems = gst_tensor_buffer_get_count (buffer);
  else
    num_mems = gst_buffer_n_memory (buffer);

  for (i = 0; i < num_mems; i++) {
    if (is_tensor)
      mem[i] = gst_tensor_buffer_get_nth_memory (buffer, i);
    else
      mem[i] = gst_buffer_get_memory (buffer, i);

    if (!gst_memory_map (mem[i], &map[i], GST_MAP_READ)) {
      nns_loge ("Cannot map the %uth memory in gst-buffer.", i);
      gst_memory_unref (mem[i]);
      num_mems = i;
      goto done;
    }

    ret = nns_edge_data_add (data_h, map[i].data, map[i].size, NULL);
    if (ret != NNS_EDGE_ERROR_NONE) {
      nns_loge ("Failed to append %uth memory into edge data.", i);
      num_mems = i + 1;
      goto done;
    }
  }

  ret = nns_edge_send (self->edge_h, data_h);
  if (ret != NNS_EDGE_ERROR_NONE)
    nns_loge ("Failed to send edge data, connection lost or internal error.");

done:
  if (data_h)
    nns_edge_data_destroy (data_h);

  for (i = 0; i < num_mems; i++) {
    gst_memory_unmap (mem[i], &map[i]);
    gst_memory_unref (mem[i]);
  }

  return GST_FLOW_OK;
}

/**
 * @brief An implementation of the set_caps vmethod in GstBaseSinkClass
 */
static gboolean
gst_edgesink_set_caps (GstBaseSink * basesink, GstCaps * caps)
{
  GstEdgeSink *sink = GST_EDGESINK (basesink);
  gchar *caps_str, *prev_caps_str, *new_caps_str;
  int set_rst;

  caps_str = gst_caps_to_string (caps);

  nns_edge_get_info (sink->edge_h, "CAPS", &prev_caps_str);
  if (!prev_caps_str) {
    prev_caps_str = g_strdup ("");
  }
  new_caps_str =
      g_strdup_printf ("%s@edge_sink_caps@%s", prev_caps_str, caps_str);
  set_rst = nns_edge_set_info (sink->edge_h, "CAPS", new_caps_str);

  g_free (prev_caps_str);
  g_free (new_caps_str);
  g_free (caps_str);

  return set_rst == NNS_EDGE_ERROR_NONE;
}

/**
 * @brief getter for the 'host' property.
 */
static gchar *
gst_edgesink_get_host (GstEdgeSink * self)
{
  return self->host;
}

/**
 * @brief setter for the 'host' property.
 */
static void
gst_edgesink_set_host (GstEdgeSink * self, const gchar * host)
{
  if (self->host)
    g_free (self->host);
  self->host = g_strdup (host);
}

/**
 * @brief getter for the 'port' property.
 */
static guint16
gst_edgesink_get_port (GstEdgeSink * self)
{
  return self->port;
}

/**
 * @brief setter for the 'port' property.
 */
static void
gst_edgesink_set_port (GstEdgeSink * self, const guint16 port)
{
  self->port = port;
}

/**
 * @brief getter for the 'connect_type' property.
 */
static nns_edge_connect_type_e
gst_edgesink_get_connect_type (GstEdgeSink * self)
{
  return self->connect_type;
}

/**
 * @brief setter for the 'connect_type' property.
 */
static void
gst_edgesink_set_connect_type (GstEdgeSink * self,
    const nns_edge_connect_type_e connect_type)
{
  self->connect_type = connect_type;
}
