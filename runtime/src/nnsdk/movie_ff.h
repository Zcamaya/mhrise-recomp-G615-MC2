// Demuxer de MP4 con FFmpeg (solo libavformat; la decodificación sigue en MediaCodec). Se usa cuando
// AMediaExtractor abre el vídeo pero no encuentra ninguna pista (visto en un Snapdragon 7+ Gen 2: el juego se
// quedaba esperando el vídeo para siempre). Si el proyecto se compila sin FFmpeg (MHRISE_FFMPEG sin definir),
// open() devuelve nullptr y todo queda como antes.
#pragma once

#include <media/NdkMediaFormat.h>

#include <cstddef>
#include <cstdint>
#include <sys/types.h>

namespace movie_ff {

struct Demuxer;

// El bloque `data` debe vivir hasta destroy(). nullptr si FFmpeg no está o no entiende el contenedor.
Demuxer* open(const uint8_t* data, size_t size);
void destroy(Demuxer*);

int track_count(Demuxer*);
AMediaFormat* track_format(Demuxer*, int track);  // nuevo; lo libera quien llama
bool select_track(Demuxer*, int track, bool on);
int next_track(Demuxer*);        // pista de la próxima muestra; -1 al acabarse
int64_t sample_time(Demuxer*);   // µs; -1 si no hay muestra
bool sample_sync(Demuxer*);
ssize_t sample_size(Demuxer*);   // -1 si no hay muestra
ssize_t read_sample(Demuxer*, uint8_t* out, size_t capacity);
bool advance(Demuxer*);          // false al acabarse
bool seek(Demuxer*, int64_t time_us, bool forward);
int64_t duration_us(Demuxer*);   // la más larga de las pistas

}  // namespace movie_ff
