// Maly serwer RTSP do testow wejscia RTSP: nadaje obraz testowy H.264 i ton AAC pod rtsp://127.0.0.1:<port>/test.
// Uzycie: rtsp_test_server.exe [port=8554]      (konczy sie po zabiciu procesu)
#include <gst/gst.h>
#include <gst/rtsp-server/rtsp-server.h>

#include <cstdio>

int main(int argc, char* argv[]) {
    gst_init(&argc, &argv);
    const char* port = argc > 1 ? argv[1] : "8554";
    GMainLoop* loop = g_main_loop_new(nullptr, FALSE);
    GstRTSPServer* server = gst_rtsp_server_new();
    gst_rtsp_server_set_service(server, port);
    GstRTSPMountPoints* mounts = gst_rtsp_server_get_mount_points(server);
    GstRTSPMediaFactory* factory = gst_rtsp_media_factory_new();
    gst_rtsp_media_factory_set_launch(factory,
        "( videotestsrc is-live=true pattern=smpte ! video/x-raw,width=1280,height=720,framerate=25/1 "
        "! videoconvert ! x264enc tune=zerolatency speed-preset=ultrafast key-int-max=25 bitrate=3000 ! rtph264pay name=pay0 pt=96 "
        "audiotestsrc is-live=true wave=sine ! audio/x-raw,rate=48000,channels=2 ! audioconvert ! avenc_aac ! rtpmp4gpay name=pay1 pt=97 )");
    gst_rtsp_media_factory_set_shared(factory, TRUE);
    gst_rtsp_mount_points_add_factory(mounts, "/test", factory);
    g_object_unref(mounts);
    if (!gst_rtsp_server_attach(server, nullptr)) {
        std::fprintf(stderr, "cannot bind port %s\n", port);
        return 1;
    }
    std::printf("rtsp://127.0.0.1:%s/test\n", port);
    std::fflush(stdout);
    g_main_loop_run(loop);
    return 0;
}
