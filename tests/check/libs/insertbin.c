/* GStreamer
 *
 * unit test for autoconvert element
 * Copyright (C) 2009 Jan Schmidt <thaytan@noraisin.net>
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
 * Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 * Boston, MA 02111-1307, USA.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <gst/gst.h>
#include <gst/check/gstcheck.h>
#include <gst/insertbin/gstinsertbin.h>

GstStaticPadTemplate sinkpad_template = GST_STATIC_PAD_TEMPLATE ("sink",        // the name of the pad
    GST_PAD_SINK,               // the direction of the pad
    GST_PAD_ALWAYS,             // when this pad will be present
    GST_STATIC_CAPS (           // the capabilities of the padtemplate
        "video/test")
    );

GstStaticPadTemplate srcpad_template = GST_STATIC_PAD_TEMPLATE ("src",  // the name of the pad
    GST_PAD_SRC,                // the direction of the pad
    GST_PAD_ALWAYS,             // when this pad will be present
    GST_STATIC_CAPS (           // the capabilities of the padtemplate
        "video/test")
    );

gint cb_count = 0;

GMutex mutex;
GCond cond;

GThread *push_thread = NULL;
gulong block_probe_id = 0;
gboolean is_blocked = FALSE;

static void
success_cb (GstInsertBin * insertbin, GstElement * element, gboolean success,
    gpointer user_data)
{
  fail_unless (g_thread_self () != push_thread);
  fail_unless (success == TRUE);
  fail_unless (GST_IS_ELEMENT (insertbin));
  fail_unless (GST_IS_ELEMENT (element));
  cb_count++;
}

static void
fail_cb (GstInsertBin * insertbin, GstElement * element, gboolean success,
    gpointer user_data)
{
  fail_unless (GST_IS_ELEMENT (insertbin));
  fail_unless (GST_IS_ELEMENT (element));
  fail_unless (success == FALSE);
  cb_count++;
}

/*
 * This is a macro so the line number of any error is more useful
 */
#define push_buffer(srcpad, count)                              \
  {                                                             \
    fail_unless (cb_count == 0);                                \
    gst_pad_push (srcpad, gst_buffer_new ());                   \
    fail_unless (g_list_length (buffers) == 1);                 \
    gst_check_drop_buffers ();                                  \
    fail_unless (cb_count == (count));                          \
    cb_count = 0;                                               \
  }

#define check_reset_cb_count(count)                             \
  {                                                             \
    fail_unless (cb_count == (count));                          \
    cb_count = 0;                                               \
  }

static gpointer
thread_push_buffer (gpointer data)
{
  GstPad *pad = data;

  gst_pad_push (pad, gst_buffer_new ());
  return NULL;
}

static GstPadProbeReturn
got_buffer_block (GstPad * pad, GstPadProbeInfo * info, gpointer data)
{
  g_mutex_lock (&mutex);
  is_blocked = TRUE;
  g_cond_broadcast (&cond);
  g_mutex_unlock (&mutex);

  return GST_PAD_PROBE_OK;
}

#define block_thread()                                                  \
{                                                                       \
  fail_unless (cb_count == 0);                                          \
  fail_unless (block_probe_id == 0);                                    \
  fail_unless (is_blocked == FALSE);                                    \
  fail_unless (push_thread == NULL);                                    \
  block_probe_id = gst_pad_add_probe (sinkpad,                           \
      GST_PAD_PROBE_TYPE_BLOCK | GST_PAD_PROBE_TYPE_BUFFER,             \
      got_buffer_block, NULL, NULL);                                    \
  push_thread = g_thread_new ("push block", thread_push_buffer, srcpad); \
  fail_unless (push_thread != NULL);                                    \
  g_mutex_lock (&mutex);                                                \
  while (is_blocked == FALSE)                                           \
    g_cond_wait (&cond, &mutex);                                        \
  g_mutex_unlock (&mutex);                                              \
}

#define unblock_thread()                                                \
{                                                                       \
  fail_unless (cb_count == 0);                                          \
  fail_unless (push_thread != NULL);                                    \
  fail_unless (is_blocked == TRUE);                                     \
  fail_unless (block_probe_id != 0);                                    \
  gst_pad_remove_probe (sinkpad, block_probe_id);                       \
  g_thread_join (push_thread);                                          \
  fail_unless (g_list_length (buffers) == 1);                           \
  gst_check_drop_buffers ();                                            \
  block_probe_id = 0;                                                   \
  push_thread = NULL;                                                   \
  is_blocked = FALSE;                                                   \
}

GST_START_TEST (test_insertbin_simple)
{
  GstElement *insertbin;
  GstElement *elem;
  GstElement *elem2;
  GstElement *elem3;
  GstElement *elem4;
  GstPad *srcpad;
  GstPad *sinkpad;

  g_mutex_init (&mutex);
  g_cond_init (&cond);

  insertbin = gst_insert_bin_new (NULL);
  fail_unless (insertbin != NULL);
  ASSERT_OBJECT_REFCOUNT (insertbin, insertbin, 1);
  srcpad = gst_check_setup_src_pad (insertbin, &srcpad_template);
  sinkpad = gst_check_setup_sink_pad (insertbin, &sinkpad_template);

  g_assert (srcpad && sinkpad);

  ASSERT_CRITICAL (gst_insert_bin_append (GST_INSERT_BIN (insertbin), NULL,
          NULL, NULL));
  ASSERT_CRITICAL (gst_insert_bin_append (GST_INSERT_BIN (insertbin), NULL,
          fail_cb, NULL));
  fail_unless (cb_count == 0);


  elem = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem, success_cb, NULL);
  check_reset_cb_count (1);

  gst_insert_bin_remove (GST_INSERT_BIN (insertbin), elem, success_cb, NULL);
  check_reset_cb_count (1);

  fail_unless (gst_pad_set_active (srcpad, TRUE));
  fail_unless (gst_pad_set_active (sinkpad, TRUE));
  fail_unless (gst_element_set_state (insertbin,
          GST_STATE_PLAYING) == GST_STATE_CHANGE_SUCCESS);

  fail_unless (cb_count == 0);
  fail_unless (buffers == NULL);

  push_buffer (srcpad, 0);

  block_thread ();
  elem = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_prepend (GST_INSERT_BIN (insertbin), elem, success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 0);

  block_thread ();
  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem, fail_cb, NULL);
  check_reset_cb_count (1);
  unblock_thread ();
  push_buffer (srcpad, 0);

  block_thread ();
  gst_insert_bin_remove (GST_INSERT_BIN (insertbin), elem, success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 0);

  block_thread ();
  elem = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem, success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 0);

  block_thread ();
  elem2 = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem2, success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 0);

  block_thread ();
  elem3 = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem3, success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 0);

  block_thread ();
  elem4 = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_prepend (GST_INSERT_BIN (insertbin), elem4, success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 0);

  block_thread ();
  gst_insert_bin_remove (GST_INSERT_BIN (insertbin), elem3, success_cb, NULL);
  gst_insert_bin_remove (GST_INSERT_BIN (insertbin), elem2, success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 0);

  block_thread ();
  elem2 = gst_element_factory_make ("identity", NULL);
  elem3 = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_insert_after (GST_INSERT_BIN (insertbin), elem2, elem,
      success_cb, NULL);
  gst_insert_bin_insert_before (GST_INSERT_BIN (insertbin), elem3, elem4,
      success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 0);

  block_thread ();
  gst_insert_bin_remove (GST_INSERT_BIN (insertbin), elem3, success_cb, NULL);
  gst_insert_bin_remove (GST_INSERT_BIN (insertbin), elem2, success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 2);
  push_buffer (srcpad, 0);

  block_thread ();
  elem2 = gst_element_factory_make ("identity", NULL);
  elem3 = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_insert_before (GST_INSERT_BIN (insertbin), elem3, elem4,
      success_cb, NULL);
  gst_insert_bin_insert_after (GST_INSERT_BIN (insertbin), elem2, elem,
      success_cb, NULL);
  unblock_thread ();
  push_buffer (srcpad, 2);
  push_buffer (srcpad, 0);

  block_thread ();
  elem = gst_bin_new (NULL);
  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem, fail_cb, NULL);
  check_reset_cb_count (1);
  unblock_thread ();

  block_thread ();
  elem = gst_bin_new (NULL);
  elem2 = gst_element_factory_make ("identity", NULL);
  gst_bin_add (GST_BIN (elem), elem2);
  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem2, fail_cb, NULL);
  check_reset_cb_count (1);
  gst_insert_bin_remove (GST_INSERT_BIN (insertbin), elem2, fail_cb, NULL);
  check_reset_cb_count (1);
  unblock_thread ();
  gst_object_unref (elem);

  push_buffer (srcpad, 0);

  block_thread ();
  elem = gst_element_factory_make ("identity", NULL);
  elem2 = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem, success_cb, NULL);
  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem2, success_cb, NULL);
  gst_insert_bin_remove (GST_INSERT_BIN (insertbin), elem2, success_cb, NULL);
  check_reset_cb_count (2);
  unblock_thread ();
  push_buffer (srcpad, 1);
  push_buffer (srcpad, 0);

  block_thread ();
  elem = gst_element_factory_make ("identity", NULL);
  elem2 = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_insert_before (GST_INSERT_BIN (insertbin), elem, elem2,
      fail_cb, NULL);
  check_reset_cb_count (1);
  unblock_thread ();
  push_buffer (srcpad, 0);
  gst_object_unref (elem2);

  fail_unless (gst_element_set_state (insertbin,
          GST_STATE_NULL) == GST_STATE_CHANGE_SUCCESS);
  gst_pad_set_active (srcpad, FALSE);
  gst_pad_set_active (sinkpad, FALSE);


  elem = gst_element_factory_make ("identity", NULL);
  gst_insert_bin_remove (GST_INSERT_BIN (insertbin), elem, fail_cb, NULL);
  check_reset_cb_count (1);

  gst_insert_bin_append (GST_INSERT_BIN (insertbin), elem, success_cb, NULL);
  check_reset_cb_count (1);

  gst_check_teardown_sink_pad (insertbin);
  gst_check_teardown_src_pad (insertbin);
  gst_check_teardown_element (insertbin);

  fail_unless (cb_count == 0);

  g_mutex_clear (&mutex);
  g_cond_clear (&cond);
}

GST_END_TEST;


static Suite *
insert_bin_suite (void)
{
  Suite *s = suite_create ("insertbin");
  TCase *tc_basic = tcase_create ("general");

  suite_add_tcase (s, tc_basic);
  tcase_add_test (tc_basic, test_insertbin_simple);

  return s;
}


GST_CHECK_MAIN (insert_bin);
