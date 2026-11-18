#include "Recovery.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>

#include <gst/gst.h>

#include "ChannelManager.h"

namespace {
bool g_enabled = false;

QString journalPath() { return QFileInfo(ChannelManager::autosavePath()).absolutePath() + QStringLiteral("/recordings.journal.json"); }

QStringList readJournal() {
    QFile f(journalPath());
    if (!f.open(QIODevice::ReadOnly)) return {};
    QStringList out;
    for (const QJsonValue& v : QJsonDocument::fromJson(f.readAll()).array()) out << v.toString();
    return out;
}

void writeJournal(const QStringList& list) {
    if (list.isEmpty()) {
        QFile::remove(journalPath());
        return;
    }
    QDir().mkpath(QFileInfo(journalPath()).absolutePath());
    QSaveFile f(journalPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(QJsonArray::fromStringList(list)).toJson());
        f.commit();
    }
}

// matroskademux wystawia pady po przeczytaniu naglowka - kazdy dostaje wlasny pad muxera.
void onDemuxPad(GstElement*, GstPad* pad, gpointer user) {
    GstElement* mux = GST_ELEMENT(user);
    gchar* name = gst_pad_get_name(pad);
    const char* tmpl = g_str_has_prefix(name, "video") ? "video_%u" : g_str_has_prefix(name, "audio") ? "audio_%u" : nullptr;
    g_free(name);
    if (!tmpl) return;
    // Kolejka na kazdy strumien jest konieczna: demukser ma jeden watek, a muxer czeka na dane ze wszystkich wejsc -
    // bez kolejek obraz i dzwiek blokuja sie nawzajem.
    GstObject* pipe = gst_element_get_parent(mux);
    GstElement* queue = gst_element_factory_make("queue", nullptr);
    GstPad* sink = gst_element_request_pad_simple(mux, tmpl);
    if (pipe && queue && sink) {
        gst_bin_add(GST_BIN(pipe), queue);
        gst_element_sync_state_with_parent(queue);
        GstPad* qSink = gst_element_get_static_pad(queue, "sink");
        GstPad* qSrc = gst_element_get_static_pad(queue, "src");
        gst_pad_link(qSrc, sink);
        gst_pad_link(pad, qSink);
        gst_object_unref(qSink);
        gst_object_unref(qSrc);
    } else if (queue) {
        gst_object_unref(queue);
    }
    if (sink) gst_object_unref(sink);
    if (pipe) gst_object_unref(pipe);
}

// Przepisanie pliku MKV: te same strumienie, bez ponownego kodowania. Uciety koniec pliku (ostatni niepelny fragment)
// konczy sie bledem demuksera - wtedy muxer dostaje koniec strumienia i zamyka to, co juz ma.
bool remuxMkv(const QString& in, const QString& out) {
    GstElement* pipe = gst_pipeline_new(nullptr);
    GstElement* src = gst_element_factory_make("filesrc", nullptr);
    GstElement* demux = gst_element_factory_make("matroskademux", nullptr);
    GstElement* mux = gst_element_factory_make("matroskamux", nullptr);
    GstElement* sink = gst_element_factory_make("filesink", nullptr);
    if (!pipe || !src || !demux || !mux || !sink) {
        for (GstElement* e : {pipe, src, demux, mux, sink})
            if (e) gst_object_unref(e);
        return false;
    }
    g_object_set(src, "location", QDir::toNativeSeparators(in).toUtf8().constData(), nullptr);
    g_object_set(sink, "location", QDir::toNativeSeparators(out).toUtf8().constData(), "sync", FALSE, nullptr);
    gst_bin_add_many(GST_BIN(pipe), src, demux, mux, sink, nullptr);
    gst_element_link(src, demux);
    gst_element_link(mux, sink);
    g_signal_connect(demux, "pad-added", G_CALLBACK(onDemuxPad), mux);

    bool ok = false, eosSent = false;
    if (gst_element_set_state(pipe, GST_STATE_PLAYING) != GST_STATE_CHANGE_FAILURE) {
        GstBus* bus = gst_element_get_bus(pipe);
        QElapsedTimer total;
        total.start();
        while (total.elapsed() < 30 * 60 * 1000) {
            GstMessage* m = gst_bus_timed_pop_filtered(bus, 100 * GST_MSECOND, GstMessageType(GST_MESSAGE_EOS | GST_MESSAGE_ERROR));
            QCoreApplication::processEvents();
            if (!m) continue;
            const bool eos = GST_MESSAGE_TYPE(m) == GST_MESSAGE_EOS;
            gst_message_unref(m);
            if (eos) {
                ok = true;
                break;
            }
            if (eosSent) break;         // drugi blad: nic wiecej sie nie da zrobic
            eosSent = true;             // uciety koniec pliku: zamknij to, co juz przeszlo
            gst_element_send_event(mux, gst_event_new_eos());
        }
        gst_object_unref(bus);
    }
    gst_element_set_state(pipe, GST_STATE_NULL);
    gst_object_unref(pipe);
    return ok && QFileInfo(out).size() > 1024;
}
}  // namespace

void Recovery::setEnabled(bool on) { g_enabled = on; }

void Recovery::begin(const QString& sidecar) {
    if (!g_enabled || sidecar.isEmpty()) return;
    QStringList list = readJournal();
    if (!list.contains(sidecar)) list << sidecar;
    writeJournal(list);
}

void Recovery::end(const QString& sidecar) {
    if (!g_enabled || sidecar.isEmpty()) return;
    QStringList list = readJournal();
    if (list.removeAll(sidecar)) writeJournal(list);
}

QStringList Recovery::pending() {
    QStringList out;
    for (const QString& s : readJournal())
        if (QFileInfo::exists(s)) out << s;
    return out;
}

void Recovery::forget(const QStringList& sidecars) {
    QStringList list = readJournal();
    for (const QString& s : sidecars) list.removeAll(s);
    writeJournal(list);
}

Recovery::Result Recovery::repair(const QString& sidecar) {
    Result r;
    r.sidecar = sidecar;
    QFile f(sidecar);
    if (!f.open(QIODevice::ReadOnly)) return r;
    QJsonObject root = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QDir dir = QFileInfo(sidecar).absoluteDir();
    for (const QJsonValue& v : root.value(QStringLiteral("parts")).toArray()) {
        const QString file = dir.absoluteFilePath(v.toObject().value(QStringLiteral("file")).toString());
        if (!QFileInfo::exists(file) || QFileInfo(file).size() == 0) continue;
        ++r.parts;
        const QString ext = QFileInfo(file).suffix().toLower();
        // Czesc zamknieta przed awaria (split) ma w opisie czas konca i jest kompletna - naprawy wymaga tylko czesc przerwana.
        const bool closed = v.toObject().contains(QStringLiteral("endMs")) && v != root.value(QStringLiteral("parts")).toArray().last();
        // Rosnacy MP4 / MOV po awarii bywa czytelny, ale nie zawsze (w probach ok. 3 z 8 plikow nie dalo sie otworzyc) i nie
        // umiemy go naprawic - dlatego nie jest tu liczony jako nienaruszony. Naprawiane sa tylko MKV.
        if (closed || ext == QLatin1String("ts")) {
            ++r.intact;
        } else if (ext == QLatin1String("mkv")) {
            const QString tmp = file + QStringLiteral(".repair.tmp");
            const QString keep = file.left(file.size() - 4) + QStringLiteral(".crashed.mkv");
            QFile::remove(tmp);
            if (remuxMkv(file, tmp) && !QFileInfo::exists(keep) && QFile::rename(file, keep) && QFile::rename(tmp, file)) {
                ++r.repaired;
            } else {
                QFile::remove(tmp);
                ++r.lost;
            }
        } else {
            ++r.lost;       // MP4 / MOV bez spisu tresci
        }
    }
    root.insert(QStringLiteral("inProgress"), false);
    root.insert(QStringLiteral("clean"), false);
    root.insert(QStringLiteral("stopReason"), QStringLiteral("program or power failure"));
    root.insert(QStringLiteral("recovered"), QJsonObject{{QStringLiteral("parts"), r.parts}, {QStringLiteral("repaired"), r.repaired},
                                                         {QStringLiteral("intact"), r.intact}, {QStringLiteral("lost"), r.lost}});
    QSaveFile out(sidecar);
    if (out.open(QIODevice::WriteOnly)) {
        out.write(QJsonDocument(root).toJson());
        out.commit();
    }
    forget({sidecar});
    return r;
}
