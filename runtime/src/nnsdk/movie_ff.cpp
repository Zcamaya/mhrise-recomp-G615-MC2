// Ver movie_ff.h.
#include "nnsdk/movie_ff.h"

#include <media/NdkMediaFormat.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <vector>

#ifdef MHRISE_FFMPEG
extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mem.h>
}
#endif

namespace movie_ff {

#ifndef MHRISE_FFMPEG

Demuxer* open(const uint8_t*, size_t) { return nullptr; }
void destroy(Demuxer*) {}
int track_count(Demuxer*) { return 0; }
AMediaFormat* track_format(Demuxer*, int) { return nullptr; }
bool select_track(Demuxer*, int, bool) { return false; }
int next_track(Demuxer*) { return -1; }
int64_t sample_time(Demuxer*) { return -1; }
bool sample_sync(Demuxer*) { return false; }
ssize_t sample_size(Demuxer*) { return -1; }
ssize_t read_sample(Demuxer*, uint8_t*, size_t) { return -1; }
bool advance(Demuxer*) { return false; }
bool seek(Demuxer*, int64_t, bool) { return false; }
int64_t duration_us(Demuxer*) { return 0; }

#else

namespace {
constexpr int kMaxTracks = 16;
const uint8_t kStart[4] = {0, 0, 0, 1};

struct MemIo {
    const uint8_t* data;
    size_t size;
    size_t pos;
};

int io_read(void* opaque, uint8_t* buf, int len) {
    auto* m = static_cast<MemIo*>(opaque);
    if (m->pos >= m->size) return AVERROR_EOF;
    size_t n = std::min<size_t>(len, m->size - m->pos);
    memcpy(buf, m->data + m->pos, n);
    m->pos += n;
    return static_cast<int>(n);
}

int64_t io_seek(void* opaque, int64_t offset, int whence) {
    auto* m = static_cast<MemIo*>(opaque);
    if (whence == AVSEEK_SIZE) return static_cast<int64_t>(m->size);
    int64_t base = 0;
    switch (whence & ~AVSEEK_FORCE) {
        case SEEK_SET: base = 0; break;
        case SEEK_CUR: base = static_cast<int64_t>(m->pos); break;
        case SEEK_END: base = static_cast<int64_t>(m->size); break;
        default: return -1;
    }
    int64_t p = base + offset;
    if (p < 0 || p > static_cast<int64_t>(m->size)) return -1;
    m->pos = static_cast<size_t>(p);
    return p;
}

void append_start_code(std::vector<uint8_t>& v) { v.insert(v.end(), kStart, kStart + 4); }

// avcC -> SPS y PPS en Annex B (lo que MediaCodec espera en csd-0 y csd-1). nal_len: bytes del prefijo de longitud de
// cada NAL en las muestras (0: la muestra ya viene en Annex B).
bool parse_avcc(const uint8_t* e, int n, std::vector<uint8_t>& sps, std::vector<uint8_t>& pps, int* nal_len) {
    if (n >= 4 && (!memcmp(e, kStart, 4) || (e[0] == 0 && e[1] == 0 && e[2] == 1))) {  // ya Annex B
        sps.assign(e, e + n);
        *nal_len = 0;
        return true;
    }
    if (n < 7 || e[0] != 1) return false;
    *nal_len = (e[4] & 3) + 1;
    int p = 5, count = e[p++] & 0x1f;
    for (int i = 0; i < count; i++) {
        if (p + 2 > n) return false;
        int len = (e[p] << 8) | e[p + 1];
        p += 2;
        if (p + len > n) return false;
        append_start_code(sps);
        sps.insert(sps.end(), e + p, e + p + len);
        p += len;
    }
    if (p >= n) return true;
    count = e[p++];
    for (int i = 0; i < count; i++) {
        if (p + 2 > n) return false;
        int len = (e[p] << 8) | e[p + 1];
        p += 2;
        if (p + len > n) return false;
        append_start_code(pps);
        pps.insert(pps.end(), e + p, e + p + len);
        p += len;
    }
    return true;
}
}  // namespace

struct Demuxer {
    std::mutex m;
    MemIo io{};
    AVIOContext* avio = nullptr;
    AVFormatContext* fmt = nullptr;
    AVPacket* pkt = nullptr;
    int map[kMaxTracks];   // pista expuesta -> stream
    int nal_len[kMaxTracks];
    int ntracks = 0;
    uint64_t selected = 0;  // bits de stream
    bool have = false, eof = false;
    std::vector<uint8_t> sample;  // la muestra actual, ya convertida
    int sample_track = -1;

    ~Demuxer() {
        if (pkt) av_packet_free(&pkt);
        if (fmt) avformat_close_input(&fmt);
        if (avio) {
            av_freep(&avio->buffer);
            avio_context_free(&avio);
        }
    }

    // Deja en `sample` la próxima muestra de una pista elegida. Con m cogido.
    void fill() {
        while (fmt && !have && !eof) {
            int r = av_read_frame(fmt, pkt);
            if (r < 0) {
                eof = true;
                break;
            }
            int track = -1;
            for (int i = 0; i < ntracks; i++)
                if (map[i] == pkt->stream_index) track = i;
            if (track < 0 || !(selected & (1ull << pkt->stream_index))) {
                av_packet_unref(pkt);
                continue;
            }
            sample.clear();
            sample_track = track;
            const uint8_t* d = pkt->data;
            int n = pkt->size;
            if (nal_len[track] > 0) {  // H.264 con prefijos de longitud -> Annex B
                int L = nal_len[track];
                int p = 0;
                while (p + L <= n) {
                    uint32_t len = 0;
                    for (int i = 0; i < L; i++) len = (len << 8) | d[p + i];
                    p += L;
                    if (len == 0 || p + static_cast<int64_t>(len) > n) break;
                    append_start_code(sample);
                    sample.insert(sample.end(), d + p, d + p + len);
                    p += static_cast<int>(len);
                }
            } else {
                sample.assign(d, d + n);
            }
            have = true;
        }
    }
};

Demuxer* open(const uint8_t* data, size_t size) {
    if (!data || size == 0) return nullptr;
    auto* d = new Demuxer;
    d->io = {data, size, 0};
    const int kBuf = 64 * 1024;
    auto* buf = static_cast<uint8_t*>(av_malloc(kBuf));
    d->avio = buf ? avio_alloc_context(buf, kBuf, 0, &d->io, io_read, nullptr, io_seek) : nullptr;
    if (!d->avio) {
        if (buf) av_free(buf);
        delete d;
        return nullptr;
    }
    d->fmt = avformat_alloc_context();
    if (!d->fmt) {
        delete d;
        return nullptr;
    }
    d->fmt->pb = d->avio;
    d->fmt->flags |= AVFMT_FLAG_CUSTOM_IO;
    int r = avformat_open_input(&d->fmt, "movie.mp4", nullptr, nullptr);
    if (r < 0) {
        printf("[movie] ffmpeg: no abre el vídeo (%d)\n", r);
        delete d;  // (avformat_open_input ya liberó fmt al fallar)
        return nullptr;
    }
    if (avformat_find_stream_info(d->fmt, nullptr) < 0) {
        printf("[movie] ffmpeg: find_stream_info falló\n");
        delete d;
        return nullptr;
    }
    d->pkt = av_packet_alloc();
    for (unsigned i = 0; i < d->fmt->nb_streams && d->ntracks < kMaxTracks; i++) {
        AVCodecParameters* p = d->fmt->streams[i]->codecpar;
        printf("[movie] ffmpeg: stream %u tipo %d códec %s %dx%d\n", i, static_cast<int>(p->codec_type),
               avcodec_get_name(p->codec_id), p->width, p->height);
        bool video = p->codec_type == AVMEDIA_TYPE_VIDEO && p->codec_id == AV_CODEC_ID_H264;
        bool audio = p->codec_type == AVMEDIA_TYPE_AUDIO && p->codec_id == AV_CODEC_ID_AAC;
        if (!video && !audio) continue;  // los demás códecs no los sabe MediaCodec con este formato
        d->nal_len[d->ntracks] = 0;
        if (video) {
            std::vector<uint8_t> sps, pps;
            int nl = 0;
            if (p->extradata && parse_avcc(p->extradata, p->extradata_size, sps, pps, &nl)) d->nal_len[d->ntracks] = nl;
        }
        d->map[d->ntracks++] = static_cast<int>(i);
    }
    printf("[movie] ffmpeg: %d pistas utilizables\n", d->ntracks);
    if (d->ntracks == 0) {
        delete d;
        return nullptr;
    }
    return d;
}

void destroy(Demuxer* d) { delete d; }

int track_count(Demuxer* d) { return d ? d->ntracks : 0; }

AMediaFormat* track_format(Demuxer* d, int track) {
    if (!d || track < 0 || track >= d->ntracks) return nullptr;
    AVStream* s = d->fmt->streams[d->map[track]];
    AVCodecParameters* p = s->codecpar;
    AMediaFormat* f = AMediaFormat_new();
    if (p->codec_type == AVMEDIA_TYPE_VIDEO) {
        AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "video/avc");
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_WIDTH, p->width);
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_HEIGHT, p->height);
        int fps = s->avg_frame_rate.den ? s->avg_frame_rate.num / s->avg_frame_rate.den : 30;
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_FRAME_RATE, fps > 0 ? fps : 30);
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 4 << 20);
        std::vector<uint8_t> sps, pps;
        int nl = 0;
        if (p->extradata && parse_avcc(p->extradata, p->extradata_size, sps, pps, &nl)) {
            if (!sps.empty()) AMediaFormat_setBuffer(f, "csd-0", sps.data(), sps.size());
            if (!pps.empty()) AMediaFormat_setBuffer(f, "csd-1", pps.data(), pps.size());
        }
    } else {
        AMediaFormat_setString(f, AMEDIAFORMAT_KEY_MIME, "audio/mp4a-latm");
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_SAMPLE_RATE, p->sample_rate);
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_CHANNEL_COUNT, p->ch_layout.nb_channels);
        AMediaFormat_setInt32(f, AMEDIAFORMAT_KEY_MAX_INPUT_SIZE, 256 << 10);
        if (p->extradata && p->extradata_size > 0) AMediaFormat_setBuffer(f, "csd-0", p->extradata, p->extradata_size);
    }
    int64_t dur = s->duration != AV_NOPTS_VALUE ? av_rescale_q(s->duration, s->time_base, AVRational{1, 1000000})
                                                : d->fmt->duration;
    if (dur > 0) AMediaFormat_setInt64(f, AMEDIAFORMAT_KEY_DURATION, dur);
    return f;
}

bool select_track(Demuxer* d, int track, bool on) {
    if (!d || track < 0 || track >= d->ntracks) return false;
    std::lock_guard lock(d->m);
    uint64_t bit = 1ull << d->map[track];
    if (on) d->selected |= bit;
    else d->selected &= ~bit;
    return true;
}

int next_track(Demuxer* d) {
    std::lock_guard lock(d->m);
    d->fill();
    return d->have ? d->sample_track : -1;
}

int64_t sample_time(Demuxer* d) {
    std::lock_guard lock(d->m);
    d->fill();
    if (!d->have) return -1;
    AVStream* s = d->fmt->streams[d->pkt->stream_index];
    int64_t ts = d->pkt->pts != AV_NOPTS_VALUE ? d->pkt->pts : d->pkt->dts;
    if (ts == AV_NOPTS_VALUE) return 0;
    int64_t start = s->start_time != AV_NOPTS_VALUE ? s->start_time : 0;
    return av_rescale_q(ts - start, s->time_base, AVRational{1, 1000000});
}

bool sample_sync(Demuxer* d) {
    std::lock_guard lock(d->m);
    d->fill();
    return d->have && (d->pkt->flags & AV_PKT_FLAG_KEY);
}

ssize_t sample_size(Demuxer* d) {
    std::lock_guard lock(d->m);
    d->fill();
    return d->have ? static_cast<ssize_t>(d->sample.size()) : -1;
}

ssize_t read_sample(Demuxer* d, uint8_t* out, size_t capacity) {
    std::lock_guard lock(d->m);
    d->fill();
    if (!d->have || d->sample.size() > capacity) return -1;
    memcpy(out, d->sample.data(), d->sample.size());
    return static_cast<ssize_t>(d->sample.size());
}

bool advance(Demuxer* d) {
    std::lock_guard lock(d->m);
    if (d->have) {
        av_packet_unref(d->pkt);
        d->have = false;
    }
    d->fill();
    return d->have;
}

bool seek(Demuxer* d, int64_t time_us, bool forward) {
    std::lock_guard lock(d->m);
    if (d->have) {
        av_packet_unref(d->pkt);
        d->have = false;
    }
    d->eof = false;
    return av_seek_frame(d->fmt, -1, time_us, forward ? 0 : AVSEEK_FLAG_BACKWARD) >= 0;
}

int64_t duration_us(Demuxer* d) {
    int64_t end = 0;
    for (int i = 0; i < d->ntracks; i++) {
        AVStream* s = d->fmt->streams[d->map[i]];
        int64_t dur = s->duration != AV_NOPTS_VALUE ? av_rescale_q(s->duration, s->time_base, AVRational{1, 1000000})
                                                    : d->fmt->duration;
        end = std::max(end, dur);
    }
    return end;
}

#endif  // MHRISE_FFMPEG

}  // namespace movie_ff
