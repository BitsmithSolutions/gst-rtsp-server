/* Bitsmith Solutions, LLC
 * Copyright (C) 2021 Bitsmith Solutions, LLC. 
 * 331 E. Truslow Ave, Fullerton, CA 92832, U.S.A All rights reserved.
 * Mark A Garcia <magarcia at bitsmithsolutions.com>
 *
 * THIS PRODUCT CONTAINS CONFIDENTIAL INFORMATION AND TRADE SECRETS OF 
 * BITSMITH SOLUTIONS, LLC. USE, DISCLOSURE OR REPRODUCTION IS PROHIBITED 
 * WITHOUT THE PRIOR EXPRESS WRITTEN PERMISSION OF BITSMITH SOLUTIONS, LLC. 
 */

#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#define DEFAULT_RTSP_PORT "7012"
#define STREAM_NAME "stream"
#define STREAM_LISTEN_PORT "11412"


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

static char *port = (char *) DEFAULT_RTSP_PORT;

int
main (int argc, char *argv[])
{
  GMainLoop *loop;
  GstRTSPServer *server;
  GstRTSPMountPoints *mounts;
  GstRTSPMediaFactory *factory;
  GError *error = NULL;
  
  gst_init (&argc, &argv);

  loop = g_main_loop_new (NULL, FALSE);

  /* create a server instance */
  server = gst_rtsp_server_new ();
  g_object_set (server, "service", port, NULL);

  /* get the mount points for this server, every server has a default object
   * that be used to map uri mount points to media factories */
  mounts = gst_rtsp_server_get_mount_points (server);

  /* make a media factory for a test stream. The default media factory can use
   * gst-launch syntax to create pipelines.
   * any launch line works as long as it contains elements named pay%d. Each
   * element with pay%d names will be a stream */
  factory = gst_rtsp_media_factory_new ();
  gst_rtsp_media_factory_set_launch (factory, 
	"( udpsrc port="STREAM_LISTEN_PORT" caps=video/mpegts,systemstream=true,packetsize=188 ! queue ! tsdemux ! queue ! h264parse config-interval=1 ! queue ! rtph264pay name=pay0 pt=96 )");
  
  //gst_rtsp_media_factory_set_launch (factory, 
	//"( udpsrc port="STREAM_LISTEN_PORT" caps=video/mpegts,systemstream=true,packetsize=188 ! queue ! rtpmp2tpay name=pay0 pt=33 )");

  gst_rtsp_media_factory_set_shared (factory, TRUE);
  gst_rtsp_mount_points_add_factory (mounts, "/"STREAM_NAME, factory);
  
  /* don't need the ref to the mapper anymore */
  g_object_unref (mounts);

  /* attach the server to the default maincontext */
  if (gst_rtsp_server_attach (server, NULL) == 0)
    goto failed;

  //g_timeout_add_seconds (2, (GSourceFunc) timeout, server);
  //g_timeout_add_seconds (10, (GSourceFunc) remove_sessions, server);

  /* start serving */
  g_print ("'RTSP Server' listening for MPEG-TS/UDP on port:'"STREAM_LISTEN_PORT"', using url 'rtsp://127.0.0.1:%s/"STREAM_NAME"'\n", port);

  g_main_loop_run (loop);

  return 0;
  
  /* ERRORS */
failed:
  {
    g_print ("Failed to attach the server. Do you need sudo privileges? \n");
    return -1;
  }
}
