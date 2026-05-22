#include <glib-unix.h>
#include <gst/gst.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

struct Player {
    GstElement* pipeline = nullptr;
    GstElement* source = nullptr;
    GstElement* decoder = nullptr;
    GstElement* convert = nullptr;
    GstElement* sink = nullptr;
    GMainLoop* loop = nullptr;
    guint sigint_id = 0;
};

void on_rtsp_pad_added(GstElement* /*src*/, GstPad* new_pad, gpointer user_data) {
    auto* p = static_cast<Player*>(user_data);
    GstPad* sink_pad = gst_element_get_static_pad(p->decoder, "sink");
    if (gst_pad_is_linked(sink_pad)) {
        gst_object_unref(sink_pad);
        return;
    }

    GstCaps* caps = gst_pad_get_current_caps(new_pad);
    GstStructure* s = gst_caps_get_structure(caps, 0);
    const char* media = gst_structure_get_string(s, "media");
    if (!media || std::strcmp(media, "video") != 0) {
        gst_caps_unref(caps);
        gst_object_unref(sink_pad);
        return;
    }

    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        std::fprintf(stderr, "Failed to link rtspsrc -> decodebin (%d)\n", ret);
    }
    gst_caps_unref(caps);
    gst_object_unref(sink_pad);
}

void on_decoded_pad_added(GstElement* /*src*/, GstPad* new_pad, gpointer user_data) {
    auto* p = static_cast<Player*>(user_data);
    GstPad* sink_pad = gst_element_get_static_pad(p->convert, "sink");
    if (gst_pad_is_linked(sink_pad)) {
        gst_object_unref(sink_pad);
        return;
    }

    GstCaps* caps = gst_pad_get_current_caps(new_pad);
    const char* name = gst_structure_get_name(gst_caps_get_structure(caps, 0));
    if (!g_str_has_prefix(name, "video/")) {
        gst_caps_unref(caps);
        gst_object_unref(sink_pad);
        return;
    }

    GstPadLinkReturn ret = gst_pad_link(new_pad, sink_pad);
    if (GST_PAD_LINK_FAILED(ret)) {
        std::fprintf(stderr, "Failed to link decodebin -> videoconvert (%d)\n", ret);
    }
    gst_caps_unref(caps);
    gst_object_unref(sink_pad);
}

gboolean on_sigint(gpointer user_data) {
    auto* p = static_cast<Player*>(user_data);
    std::printf("\nInterrupt received, shutting down.\n");
    p->sigint_id = 0; // second Ctrl+C falls through to default handler
    g_main_loop_quit(p->loop);
    return G_SOURCE_REMOVE;
}

gboolean on_bus_message(GstBus* /*bus*/, GstMessage* msg, gpointer user_data) {
    auto* p = static_cast<Player*>(user_data);
    switch (GST_MESSAGE_TYPE(msg)) {
        case GST_MESSAGE_ERROR: {
            GError* err = nullptr;
            gchar* dbg = nullptr;
            gst_message_parse_error(msg, &err, &dbg);
            const bool window_closed =
                err->domain == GST_RESOURCE_ERROR && err->message &&
                std::strstr(err->message, "Output window was closed") != nullptr;
            if (window_closed) {
                std::printf("Window closed, shutting down.\n");
            } else {
                std::fprintf(stderr, "ERROR from %s: %s\n",
                             GST_OBJECT_NAME(msg->src), err->message);
                if (dbg) std::fprintf(stderr, "  debug: %s\n", dbg);
            }
            g_error_free(err);
            g_free(dbg);
            g_main_loop_quit(p->loop);
            break;
        }
        case GST_MESSAGE_EOS:
            std::printf("End of stream.\n");
            g_main_loop_quit(p->loop);
            break;
        case GST_MESSAGE_STATE_CHANGED:
            if (GST_MESSAGE_SRC(msg) == GST_OBJECT(p->pipeline)) {
                GstState old_s, new_s;
                gst_message_parse_state_changed(msg, &old_s, &new_s, nullptr);
                std::printf("Pipeline state: %s -> %s\n",
                            gst_element_state_get_name(old_s),
                            gst_element_state_get_name(new_s));
            }
            break;
        default:
            break;
    }
    return TRUE;
}

} // namespace

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);

    if (argc < 2) {
        std::fprintf(stderr,
                     "Usage: %s <rtsp-url>\n"
                     "  e.g. rtsp://rpi5.local:8555/cam\n"
                     "       rtsp://rpi5.local:8554/hdmi\n",
                     argv[0]);
        return 1;
    }
    const char* url = argv[1];

    Player p;
    p.pipeline = gst_pipeline_new("player");
    p.source = gst_element_factory_make("rtspsrc", "source");
    p.decoder = gst_element_factory_make("decodebin", "decoder");
    p.convert = gst_element_factory_make("videoconvert", "convert");
    p.sink = gst_element_factory_make("autovideosink", "sink");

    if (!p.pipeline || !p.source || !p.decoder || !p.convert || !p.sink) {
        std::fprintf(stderr, "Failed to create one or more GStreamer elements.\n");
        return 1;
    }

    // Low-latency RTSP settings.
    g_object_set(p.source,
                 "location", url,
                 "latency", 0,
                 "protocols", 0x4, // GST_RTSP_LOWER_TRANS_UDP
                 "buffer-mode", 0, // none
                 "do-retransmission", FALSE,
                 "drop-on-latency", TRUE,
                 nullptr);

    // Drop the clock sync on the sink so frames render as soon as decoded.
    g_object_set(p.sink, "sync", FALSE, nullptr);

    gst_bin_add_many(GST_BIN(p.pipeline),
                     p.source, p.decoder, p.convert, p.sink, nullptr);

    if (!gst_element_link(p.convert, p.sink)) {
        std::fprintf(stderr, "Failed to link videoconvert -> sink.\n");
        gst_object_unref(p.pipeline);
        return 1;
    }

    g_signal_connect(p.source, "pad-added", G_CALLBACK(on_rtsp_pad_added), &p);
    g_signal_connect(p.decoder, "pad-added", G_CALLBACK(on_decoded_pad_added), &p);

    p.loop = g_main_loop_new(nullptr, FALSE);
    GstBus* bus = gst_element_get_bus(p.pipeline);
    guint bus_id = gst_bus_add_watch(bus, on_bus_message, &p);
    p.sigint_id = g_unix_signal_add(SIGINT, on_sigint, &p);

    std::printf("Playing %s\n", url);
    GstStateChangeReturn ret = gst_element_set_state(p.pipeline, GST_STATE_PLAYING);
    if (ret == GST_STATE_CHANGE_FAILURE) {
        std::fprintf(stderr, "Failed to set pipeline to PLAYING.\n");
        if (p.sigint_id) g_source_remove(p.sigint_id);
        g_source_remove(bus_id);
        gst_object_unref(bus);
        gst_element_set_state(p.pipeline, GST_STATE_NULL);
        gst_object_unref(p.pipeline);
        g_main_loop_unref(p.loop);
        return 1;
    }

    g_main_loop_run(p.loop);

    if (p.sigint_id) g_source_remove(p.sigint_id);
    g_source_remove(bus_id);
    gst_object_unref(bus);
    gst_element_set_state(p.pipeline, GST_STATE_NULL);
    gst_object_unref(p.pipeline);
    g_main_loop_unref(p.loop);
    return 0;
}
