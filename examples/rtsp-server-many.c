/* Bitsmith Solutions, LLC
 * Copyright (C) 2021 Bitsmith Solutions, LLC. 
 * 331 E. Truslow Ave, Fullerton, CA 92832, U.S.A All rights reserved.
 * Mark A Garcia <magarcia at bitsmithsolutions.com>
 *
 * THIS PRODUCT CONTAINS CONFIDENTIAL INFORMATION AND TRADE SECRETS OF 
 * BITSMITH SOLUTIONS, LLC. USE, DISCLOSURE OR REPRODUCTION IS PROHIBITED 
 * WITHOUT THE PRIOR EXPRESS WRITTEN PERMISSION OF BITSMITH SOLUTIONS, LLC. 
 *
 * https://gstreamer.freedesktop.org/documentation/gst-rtsp-server/rtsp-address-pool.html?gi-language=c
 * https://gstreamer.freedesktop.org/documentation/rtsplib/gstrtsptransport.html?gi-language=c#enumerations
 */

#include <gst/gst.h>

#include <gst/rtsp-server/rtsp-server.h>

#include <gio/gio.h>

#define DEBUG 1
#define BLOCK_SIZE 1024

#define NUM_OF_CHANNELS 12

#define DEFAULT_RTSP_PORT "554"
#define STREAM1_LISTEN_PORT 11401

struct ZuRtspChannel
{
  gchar name[16];
  gchar mountPath[16];
  gchar listenPort[16];
  int controlPort;
  gchar rtspUrl[64];
  gchar pipelineLaunch[256];
  GstRTSPMediaFactory *factoryStream;
  GstRTSPAddressPool *pool;
  GstRTSPMedia *media;
  GstRTSPStream *stream;
  GstState state;
};

struct ZuRtspServerContext
{
  gchar rtspListenPort[8];
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  struct ZuRtspChannel *channel[16];
  GstRTSPMediaFactory *factory;
};

static gboolean
remove_func (GstRTSPSessionPool * pool, GstRTSPSession * session,
    GstRTSPServer * server)
{
  return GST_RTSP_FILTER_REMOVE;
}

static gboolean
remove_sessions (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  g_print ("removing all sessions\n");
  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_filter (pool,
      (GstRTSPSessionPoolFilterFunc) remove_func, server);
  g_object_unref (pool);

  return FALSE;
}

static gboolean
timeout (GstRTSPServer * server)
{
  GstRTSPSessionPool *pool;

  pool = gst_rtsp_server_get_session_pool (server);
  gst_rtsp_session_pool_cleanup (pool);
  g_object_unref (pool);

  return TRUE;
}

static void
new_state (GstRTSPMedia * media, gint * object, gpointer data)
{

  struct ZuRtspChannel *rtspChannel = (struct ZuRtspChannel *) data;
  rtspChannel->state = (GstState) object;

  if (DEBUG)
    g_print ("signal triggered: new-state: %s, Stream: %s\n",
        gst_element_state_get_name (rtspChannel->state),
        (char *) rtspChannel->name);
}

static void
target_state (GstRTSPMedia * media, gint * object, gpointer data)
{

  struct ZuRtspChannel *rtspChannel = (struct ZuRtspChannel *) data;
  rtspChannel->state = (GstState) object;

  if (DEBUG)
    g_print ("signal triggered: target-state: %s, Stream: %s\n",
        gst_element_state_get_name (rtspChannel->state),
        (char *) rtspChannel->name);

}

static void
new_stream (GstRTSPMedia * media, GstRTSPStream * rtspStream, gpointer data)
{

  struct ZuRtspChannel *rtspChannel = (struct ZuRtspChannel *) data;
  rtspChannel->stream = rtspStream;

  if (DEBUG)
    g_print ("signal triggered: new-stream, Stream: %s\n",
        (char *) rtspChannel->name);
}

static void
removed_stream (GstRTSPMedia * media, GstRTSPStream * rtspStream, gpointer data)
{

  struct ZuRtspChannel *rtspChannel = (struct ZuRtspChannel *) data;
  rtspChannel->stream = rtspStream;

  if (DEBUG)
    g_print ("signal triggered: removed-stream, Stream: %s\n",
        (char *) rtspChannel->name);
}

static void
prepared (GstRTSPMedia * media, gpointer data)
{

  struct ZuRtspChannel *rtspChannel = (struct ZuRtspChannel *) data;

  if (DEBUG)
    g_print ("signal triggered: prepared, Stream: %s\n",
        (char *) rtspChannel->name);
}

static void
unprepared (GstRTSPMedia * media, gpointer data)
{

  struct ZuRtspChannel *rtspChannel = (struct ZuRtspChannel *) data;

  if (DEBUG)
    g_print ("signal triggered: unprepared, Stream: %s\n",
        (char *) rtspChannel->name);
}

static void
media_constructed (GstRTSPMediaFactory * factory, GstRTSPMedia * media,
    gpointer data)
{
  struct ZuRtspChannel *rtspChannel = (struct ZuRtspChannel *) data;
  rtspChannel->media = media;

  if (DEBUG)
    g_print ("signal triggered: media_constructed: %s\n",
        (char *) rtspChannel->name);

  g_signal_connect (media, "new-state", (GCallback) new_state, data);
  g_signal_connect (media, "new-stream", (GCallback) new_stream, data);
  g_signal_connect (media, "prepared", (GCallback) prepared, data);
  g_signal_connect (media, "unprepared", (GCallback) unprepared, data);
  g_signal_connect (media, "removed-stream", (GCallback) removed_stream, data);
  g_signal_connect (media, "target-state", (GCallback) target_state, data);
}

static void
media_configure (GstRTSPMediaFactory * factory, GstRTSPMedia * media,
    gpointer data)
{

  struct ZuRtspChannel *rtspChannel = (struct ZuRtspChannel *) data;
  rtspChannel->media = media;

  if (DEBUG)
    g_print ("signal triggered: media-configure: %s\n",
        (char *) rtspChannel->name);
}

static gboolean
gio_read_socket (GIOChannel * channel, GIOCondition condition, gpointer data)
{

  struct ZuRtspChannel *rtspChannel = (struct ZuRtspChannel *) data;

  char buf[1024];
  gsize bytes_read;
  GError *error = NULL;

  if (condition & G_IO_HUP)
    return FALSE;               /* this channel is done */


  g_io_channel_read_chars (channel, buf, sizeof (buf), &bytes_read, &error);
  g_assert (error == NULL);
  buf[bytes_read] = '\0';
  if (DEBUG) {
    g_printf ("Read Socket: %s\n", buf);
    g_printf ("Stream Name: %s\n", rtspChannel->name);
  }

  if (g_strrstr (buf, "\"pause\"")) {
    g_printf ("Valid Message: %s\n", buf);
    if (rtspChannel->state == GST_STATE_PLAYING)
      gst_rtsp_media_set_pipeline_state (rtspChannel->media, GST_STATE_PAUSED);
  }

  return TRUE;
}

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GError *error = NULL;
  struct ZuRtspServerContext *serverContext =
      g_new (struct ZuRtspServerContext, 1);
  //gchar* pipelineLaunchStr = "( udpsrc port=%s caps=video/mpegts,systemstream=true,packetsize=188 ! queue ! rtpmp2tpay name=pay0 pt=33 )";
  gchar *pipelineLaunchStr =
      "( udpsrc port=%s caps=video/mpegts,systemstream=true,packetsize=188 ! queue ! tsdemux ! h264parse config-interval=1 ! rtph264pay name=pay0 pt=96 )";

  gst_init (&argc, &argv);
  loop = g_main_loop_new (NULL, FALSE);

  g_stpcpy (serverContext->rtspListenPort, DEFAULT_RTSP_PORT);

  /* create a server instance */
  serverContext->server = gst_rtsp_server_new ();
  g_object_set (serverContext->server, "service", serverContext->rtspListenPort,
      NULL);

  serverContext->mounts =
      gst_rtsp_server_get_mount_points (serverContext->server);

  for (int channelIndex = 0; channelIndex < NUM_OF_CHANNELS; channelIndex++) {

    serverContext->channel[channelIndex] = g_new (struct ZuRtspChannel, 1);

    g_sprintf (serverContext->channel[channelIndex]->name, "stream%d",
        channelIndex + 1);
    g_sprintf (serverContext->channel[channelIndex]->mountPath, "/%s",
        serverContext->channel[channelIndex]->name);

    serverContext->channel[channelIndex]->factoryStream =
        gst_rtsp_media_factory_new ();

    g_sprintf (serverContext->channel[channelIndex]->listenPort, "114%02d",
        channelIndex + 1);
    g_sprintf (serverContext->channel[channelIndex]->pipelineLaunch,
        pipelineLaunchStr, serverContext->channel[channelIndex]->listenPort);

    gst_rtsp_media_factory_set_launch (serverContext->channel[channelIndex]->
        factoryStream, serverContext->channel[channelIndex]->pipelineLaunch);
    gst_rtsp_media_factory_set_shared (serverContext->channel[channelIndex]->
        factoryStream, TRUE);

    g_signal_connect (serverContext->channel[channelIndex]->factoryStream,
        "media-constructed", (GCallback) media_constructed,
        serverContext->channel[channelIndex]);
    g_signal_connect (serverContext->channel[channelIndex]->factoryStream,
        "media-configure", (GCallback) media_configure,
        serverContext->channel[channelIndex]);

    /* make a new address pool */
    serverContext->channel[channelIndex]->pool = gst_rtsp_address_pool_new ();
    gst_rtsp_media_factory_set_address_pool (serverContext->channel
        [channelIndex]->factoryStream,
        serverContext->channel[channelIndex]->pool);
    gst_rtsp_media_factory_set_protocols (serverContext->channel[channelIndex]->
        factoryStream, GST_RTSP_LOWER_TRANS_UDP);
    g_object_unref (serverContext->channel[channelIndex]->pool);

    gst_rtsp_mount_points_add_factory (serverContext->mounts,
        serverContext->channel[channelIndex]->mountPath,
        serverContext->channel[channelIndex]->factoryStream);
    serverContext->channel[channelIndex]->controlPort = 11421 + channelIndex;

    /* start serving */
    g_printf
        ("'RTSP Server%d' listening for MPEG-TS/UDP on port:'%s', using url 'rtsp://127.0.0.1:%s%s', control port: %d\n",
        channelIndex + 1, serverContext->channel[channelIndex]->listenPort,
        serverContext->rtspListenPort,
        serverContext->channel[channelIndex]->mountPath,
        serverContext->channel[channelIndex]->controlPort);

    GSocket *s_udp;
    GError *err = NULL;
    int idIdle = -1, dataI = 0;
    GSocketAddress *gsockAddr =
        G_SOCKET_ADDRESS (g_inet_socket_address_new (g_inet_address_new_any
            (G_SOCKET_FAMILY_IPV4),
            serverContext->channel[channelIndex]->controlPort)
        );
    s_udp = g_socket_new (G_SOCKET_FAMILY_IPV4,
        G_SOCKET_TYPE_DATAGRAM, G_SOCKET_PROTOCOL_UDP, &err);
    g_assert (err == NULL);

    if (s_udp == NULL) {
      g_print ("ERROR");
      exit (1);
    }

    if (g_socket_bind (s_udp, gsockAddr, TRUE, NULL) == FALSE) {
      g_print ("Error bind\n");
      exit (1);
    }

    g_assert (err == NULL);
    int fd = g_socket_get_fd (s_udp);
    GIOChannel *gio_channel = g_io_channel_unix_new (fd);
    guint source =
        g_io_add_watch (gio_channel, G_IO_IN, (GIOFunc) gio_read_socket,
        serverContext->channel[channelIndex]);

    g_io_channel_unref (gio_channel);
  }

  /* don't need the ref to the mapper anymore */
  g_object_unref (serverContext->mounts);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (serverContext->server, NULL) == 0)
    goto failed;

  g_timeout_add_seconds (2, (GSourceFunc) timeout, serverContext->server);
  g_timeout_add_seconds (10, (GSourceFunc) remove_sessions,
      serverContext->server);

  g_main_loop_run (loop);

  return 0;

  /* ERRORS */
failed:
  {
    g_print ("Failed to attach the server. Do you need sudo privileges? \n");
    return -1;
  }
}
