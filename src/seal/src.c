#include <string.h>
#include <stdlib.h>
#include <al/al.h>
#include <al/efx.h>
#include <seal/src.h>
#include <seal/core.h>
#include <seal/buf.h>
#include <seal/stream.h>
#include <seal/efs.h>
#include <seal/err.h>
#include "threading.h"

typedef void queue_op_t(unsigned int, int, unsigned int*);

enum
{
    /* 2^24=16777216. */
    CHUNK_STORAGE_CAP = 16777216,
    /* LCM(36, 1024)=9216. */
    MIN_CHUNK_SIZE = 9216
};

static const size_t MIN_QUEUE_SIZE     = 2;
static const size_t MAX_QUEUE_SIZE     = 63;
static const size_t DEFAULT_QUEUE_SIZE = 3;
static const size_t DEFAULT_CHUNK_SIZE = MIN_CHUNK_SIZE << 2;
static const size_t MAX_CHUNK_SIZE     = CHUNK_STORAGE_CAP -
                                         CHUNK_STORAGE_CAP % MIN_CHUNK_SIZE;

/*
 * Checks if `val` is in the closed interval [`lower_bound`, `upper_bound`].
 */
static
seal_err_t
check_val_limit(int val, int lower_bound, int upper_bound)
{
    if (val < lower_bound)
        return SEAL_BAD_VAL;
    else if (val > upper_bound)
        return SEAL_BAD_VAL;
    else
        return SEAL_OK;
}

/*
 * Called when the state of a source is preemptively changed by a caller.
 */
static
seal_err_t
on_preemptive_state_change(seal_src_t* src)
{
    seal_err_t err;
    seal_src_state_t state;

    if ((err = seal_get_src_state(src, &state)) != SEAL_OK)
        return err;

    /* Set `early_stop` to true after playing so it will only be set to false
     * when playback ended with the end of stream reached or the state is
     * preemptively changed.
     */
    src->early_stop = state == SEAL_PLAYING ? 1 : 0;

    return SEAL_OK;
}

/*
 * Changes the source state by calling one of the four operations on a source.
 */
static
seal_err_t
change_state(seal_src_t* src, void (*op)(unsigned int))
{
    seal_err_t err;

    op(src->id);
    if ((err = _seal_get_openal_err()) != SEAL_OK)
        return err;

    return on_preemptive_state_change(src);
}

static
seal_err_t
set3f(seal_src_t* src, int key, float x, float y, float z)
{
    alSource3f(src->id, key, x, y, z);

    return _seal_get_openal_err();
}

static
seal_err_t
get3f(seal_src_t* src, int key, float* px, float* py, float* pz)
{
    alGetSource3f(src->id, key, px, py, pz);

    return _seal_get_openal_err();
}

static
void
wait4updater(seal_src_t* src)
{
    if (src->updater != 0) {
        _seal_join_thread(src->updater);
        src->updater = 0;
    }
}

/*
 * The main routine for updater threads.
 */
static
void*
update(void* args)
{
    seal_src_t* src = args;
    seal_err_t err = SEAL_OK;

    while (alIsSource(src->id)) {
        seal_src_state_t state;

        /* Check source state before checking if interrupted by caller. */
        if ((err = seal_get_src_state(src, &state)) != SEAL_OK)
           break;
        if (state != SEAL_PLAYING) {
            /* Early stopping, most likely due to I/O load. Restart playing. */
            if (src->early_stop) {
                if ((err = change_state(src, alSourcePlay)) != SEAL_OK)
                    break;
            } else {
                break;
            }
        }
        if ((err = seal_update_src(src)) != SEAL_OK)
            break;

        _seal_sleep(50);
    }

    return (void*) err;
}

static
seal_err_t
queue_op(seal_src_t* src, int nbufs, unsigned int* bufs, queue_op_t* op)
{
    op(src->id, nbufs, bufs);

    return _seal_get_openal_err();
}

static
seal_err_t
queue_bufs(seal_src_t* src, int nbufs, unsigned int* bufs)
{
    return queue_op(src, nbufs, bufs, (queue_op_t*) alSourceQueueBuffers);
}

static
seal_err_t
unqueue_bufs(seal_src_t* src, int nbufs, unsigned int* bufs)
{
    return queue_op(src, nbufs, bufs, alSourceUnqueueBuffers);
}

/*
 * Cleans the queue and free any allocated buffers in the queue. This function
 * assumes the source is stopped at the time of calling.
 */
static
seal_err_t
clean_queue(seal_src_t* src)
{
    int nbufs_processed;
    unsigned int* bufs;
    seal_err_t err;

    /* Do not let the updater touch anything when cleaning the queue. */
    wait4updater(src);
    err = _seal_geti(
        src,
        AL_BUFFERS_PROCESSED,
        &nbufs_processed,
        alGetSourcei
    );
    if (err != SEAL_OK)
        return err;

    bufs = malloc(sizeof (unsigned int) * nbufs_processed);
    if (bufs == 0)
        return SEAL_CANNOT_ALLOC_MEM;

    if ((err = unqueue_bufs(src, nbufs_processed, bufs)) == SEAL_OK)
        err = _seal_delete_objs(nbufs_processed, bufs, alDeleteBuffers);

    free(bufs);

    return err;
}

/*
 * Cleans the queue after stopping the source. A stopped source will have all
 * the buffers in its queue marked as processed so that they can be unqueued.
 * This function assumes the source is playing at the time of calling.
 */
static
seal_err_t
stop_then_clean_queue(seal_src_t* src)
{
    seal_err_t err;

    if ((err = change_state(src, alSourceStop)) != SEAL_OK)
        return err;

    return clean_queue(src);
}

static
seal_err_t
restart_queuing(seal_src_t* src)
{
    seal_err_t err;

    if ((err = stop_then_clean_queue(src)) != SEAL_OK)
        return err;

    return seal_rewind_stream(src->stream);
}

/*
 * This does the same thing as `stop_then_clean_queue` except it works for
 * sources in any state at the time of calling.
 */
static
seal_err_t
empty_queue(seal_src_t* src)
{
    seal_err_t err;

    /* Need to be playing first in order to become stopped. */
    if ((err = change_state(src, alSourcePlay)) != SEAL_OK)
        return err;

    return stop_then_clean_queue(src);
}

static
seal_err_t
ensure_queue_empty(seal_src_t* src)
{
    if (src->stream != 0)
        return empty_queue(src);

    return SEAL_OK;
}

seal_err_t
SEAL_API
seal_init_src(seal_src_t* src)
{
    seal_err_t err = _seal_init_obj(src, alGenSources);

    if (err == SEAL_OK) {
        src->buf = 0;
        src->stream = 0;
        /* The id of the thread that is updating the source. */
        src->updater = 0;
        src->chunk_size = DEFAULT_CHUNK_SIZE;
        src->queue_size = DEFAULT_QUEUE_SIZE;
        src->looping = 0;
        src->automatic = 1;
        src->early_stop = 0;
    }

    return err;
}

seal_err_t
SEAL_API
seal_destroy_src(seal_src_t* src)
{
    seal_err_t err;

    if (alIsSource(src->id)) {
        if ((err = ensure_queue_empty(src)) != SEAL_OK)
            return err;
        err = _seal_delete_objs(1, &src->id, alDeleteSources);
        if (err != SEAL_OK)
            return err;
    }

    return SEAL_OK;
}

seal_err_t
SEAL_API
seal_play_src(seal_src_t* src)
{
    if (src->stream != 0) {
        seal_src_state_t state;
        seal_err_t err = seal_get_src_state(src, &state);
        if (err != SEAL_OK)
            return err;
        if (state == SEAL_PLAYING) {
            /* Source and its updater will be stopped after this. */
            if ((err = restart_queuing(src)) != SEAL_OK)
                return err;
        } else {
            /* In case the old updater is not done. */
            wait4updater(src);
        }
        /* Stream some data so plackback can start immediately. */
        if ((err = seal_update_src(src)) != SEAL_OK)
            return err;
        /* Actually start playing. */
        if ((err = change_state(src, alSourcePlay)) != SEAL_OK)
            return err;
        /* Create background updater for automatic sources. */
        if (src->automatic)
            src->updater = _seal_create_thread(update, src);
        return SEAL_OK;
    } else {
        return change_state(src, alSourcePlay);
    }
}

seal_err_t
SEAL_API
seal_pause_src(seal_src_t* src)
{
    return change_state(src, alSourcePause);
}

seal_err_t
SEAL_API
seal_stop_src(seal_src_t* src)
{
    seal_err_t err;

    if ((err = change_state(src, alSourceStop)) != SEAL_OK)
       return err;

    if (src->stream != 0)
        /* Already stopped so all buffers are proccessed. */
        if ((err = clean_queue(src)) == SEAL_OK)
            err = seal_rewind_stream(src->stream);

    return err;
}

seal_err_t
SEAL_API
seal_rewind_src(seal_src_t* src)
{
    if (src->stream != 0) {
        seal_src_state_t state;
        seal_err_t err = seal_get_src_state(src, &state);
        if (err != SEAL_OK)
            return err;
        if (state == SEAL_PLAYING || state == SEAL_PAUSED)
            if ((err = restart_queuing(src)) != SEAL_OK)
                return err;
    }

    return change_state(src, alSourceRewind);
}

seal_err_t
SEAL_API
seal_detach_src_audio(seal_src_t* src)
{
    seal_err_t err;

    if ((err = ensure_queue_empty(src)) != SEAL_OK)
        return err;

    /* Sets the state to `SEAL_INITIAL' for consistency. */
    if ((err = change_state(src, alSourceRewind)) != SEAL_OK)
        return err;

    if ((err = _seal_seti(src, AL_BUFFER, AL_NONE, alSourcei)) != SEAL_OK)
        return err;

    if ((err = on_preemptive_state_change(src)) != SEAL_OK)
        return err;

    src->buf = 0;
    src->stream = 0;

    return err;
}

seal_err_t
SEAL_API
seal_move_src(seal_src_t* src)
{
    seal_err_t err;
    float pos[3], vel[3];

    if ((err = seal_get_src_pos(src, pos, pos + 1, pos + 2)) != SEAL_OK)
        return err;
    if ((err = seal_get_src_vel(src, vel, vel + 1, vel + 2)) != SEAL_OK)
        return err;
    err = seal_set_src_pos(
        src,
        pos[0] + vel[0],
        pos[1] + vel[1],
        pos[2] + vel[2]
    );
    if (err != SEAL_OK)
        return err;

    return SEAL_OK;
}

seal_err_t
SEAL_API
seal_set_src_buf(seal_src_t* src, seal_buf_t* buf)
{
    seal_err_t err;

    /* Make sure `src' is not currently a streaming source. */
    if (src->stream != 0)
        return SEAL_MIXING_SRC_TYPE;

    /* Carry the previous looping state over for static sources. */
    err = _seal_seti(src, AL_LOOPING, src->looping, alSourcei);
    if (err != SEAL_OK)
        return err;

    if ((err = _seal_seti(src, AL_BUFFER, buf->id, alSourcei)) == SEAL_OK)
        src->buf = buf;

    return err;
}

seal_err_t
SEAL_API
seal_set_src_stream(seal_src_t* src, seal_stream_t* stream)
{
    seal_err_t err;

    if (stream == src->stream)
        return SEAL_OK;
    /* Make sure `src' is not currently a static source. */
    if (src->buf != 0)
        return SEAL_MIXING_SRC_TYPE;
    /* Cannot associate an unopened stream. */
    if (stream->id == 0)
        return SEAL_STREAM_UNOPENED;
    /* Cannot associate a stream with a different audio format. */
    if (src->stream != 0 && memcmp(&stream->attr, &src->stream->attr,
                                   sizeof (seal_raw_attr_t)) != 0)
        return SEAL_MIXING_STREAM_FMT;

    /* Never use AL_LOOPING for streaming sources. */
    if ((err = _seal_seti(src, AL_LOOPING, 0, alSourcei)) != SEAL_OK)
        return err;

    src->stream = stream;

    /* Immediately update the queue to become `AL_STREAMING'. */
    return seal_update_src(src);
}

seal_err_t
SEAL_API
seal_feed_efs(seal_src_t* src, seal_efs_t* slot, int index)
{
    alSource3i(
        src->id,
        AL_AUXILIARY_SEND_FILTER,
        slot->id,
        index,
        AL_FILTER_NULL
    );

    return _seal_get_openal_err();
}

seal_err_t
SEAL_API
seal_update_src(seal_src_t* src)
{
    unsigned int buf;
    size_t nbytes_streamed;
    seal_raw_t raw;
    seal_err_t err;

    if (!alIsSource(src->id))
        return SEAL_OK;

    if (src->stream == 0)
        return SEAL_OK;
    /* If another updater is running. */
    if (src->updater != 0 && !_seal_calling_thread_is(src->updater))
        return SEAL_OK;

    /* Set the desired size of each chunk. */
    raw.size = src->chunk_size;

    for (;;) {
        int nqueued, nprocessed;

        err = _seal_geti(src, AL_BUFFERS_QUEUED, &nqueued, alGetSourcei);
        if (err != SEAL_OK)
            return err;
        err = _seal_geti(
            src,
            AL_BUFFERS_PROCESSED,
            &nprocessed,
            alGetSourcei
        );
        if (err != SEAL_OK)
            return err;

        /* Remove processed buffers from the queue if possible. */
        if (nprocessed > 0) {
            if ((err = unqueue_bufs(src, 1, &buf)) != SEAL_OK)
                return err;
            /* The queue is full even after the removal. */
            if ((size_t) nqueued >= src->queue_size) {
                err = _seal_delete_objs(1, &buf, alDeleteBuffers);
                if (err != SEAL_OK)
                    return err;
                continue;
            }
        /* The queue is too short. */
        } else if ((size_t) nqueued < src->queue_size) {
            if ((err = _seal_gen_objs(1, &buf, alGenBuffers)) != SEAL_OK)
                return err;
        /* The queue is full and nothing is processed. */
        } else {
            return SEAL_OK;
        }

start_streaming:
        err = seal_stream(src->stream, &raw, &nbytes_streamed);
        if (err != SEAL_OK)
            break;
        if (nbytes_streamed > 0) {
            /* Fill or refill the current buffer. */
            err = _seal_raw2buf(buf, &raw);
            free(raw.data);
            if (err != SEAL_OK)
                break;
            if ((err = queue_bufs(src, 1, &buf)) != SEAL_OK)
                break;
        /* Rewind the stream if looping. */
        } else if (src->looping) {
            seal_rewind_stream(src->stream);
            raw.size = src->chunk_size;
            goto start_streaming;
        /* End of stream reached. */
        } else {
            src->early_stop = 0;
            break;
        }
    } /* for (;;) */

    if (err == SEAL_OK)
        return _seal_delete_objs(1, &buf, alDeleteBuffers);
    else {
        _seal_delete_objs(1, &buf, alDeleteBuffers);
        return err;
    }
}

seal_err_t
SEAL_API
seal_set_src_queue_size(seal_src_t* src, size_t size)
{
    seal_err_t err;

    err = check_val_limit(size, MIN_QUEUE_SIZE, MAX_QUEUE_SIZE);
    if (err == SEAL_OK)
        src->queue_size = size;

    return err;
}

seal_err_t
SEAL_API
seal_set_src_chunk_size(seal_src_t* src, size_t size)
{
    seal_err_t err;

    err = check_val_limit(size, MIN_CHUNK_SIZE, MAX_CHUNK_SIZE);
    if (err == SEAL_OK)
        src->chunk_size = size / MIN_CHUNK_SIZE * MIN_CHUNK_SIZE;

    return err;
}

seal_err_t
SEAL_API
seal_set_src_pos(seal_src_t* src, float x, float y, float z)
{
    return set3f(src, AL_POSITION, x, y, z);
}

seal_err_t
SEAL_API
seal_set_src_vel(seal_src_t* src, float x, float y, float z)
{
    return set3f(src, AL_VELOCITY, x, y, z);
}

seal_err_t
SEAL_API
seal_set_src_pitch(seal_src_t* src, float pitch)
{
    return _seal_setf(src, AL_PITCH, pitch, alSourcef);
}

seal_err_t
SEAL_API
seal_set_src_gain(seal_src_t* src, float gain)
{
    return _seal_setf(src, AL_GAIN, gain, alSourcef);
}

seal_err_t
SEAL_API
seal_set_src_auto(seal_src_t* src, char automatic)
{
    src->automatic = automatic != 0;

    return SEAL_OK;
}

seal_err_t
SEAL_API
seal_set_src_relative(seal_src_t* src, char relative)
{
    return _seal_seti(src, AL_SOURCE_RELATIVE, relative != 0, alSourcei);
}

seal_err_t
SEAL_API
seal_set_src_looping(seal_src_t* src, char looping)
{
    looping = looping != 0;
    /*
     * Streaming does not work with OpenAL's looping as the queuing buffers
     * will never become `processed' when `AL_LOOPING' is true, so set
     * `AL_LOOPING' only for static sources.
     */
    if (src->stream == 0) {
        seal_err_t err = _seal_seti(src, AL_LOOPING, looping, alSourcei);
        if (err != SEAL_OK)
            return err;
    }

    src->looping = looping;

    return SEAL_OK;
}

seal_buf_t*
SEAL_API
seal_get_src_buf(seal_src_t* src)
{
    return src->buf;
}

seal_stream_t*
SEAL_API
seal_get_src_stream(seal_src_t* src)
{
    return src->stream;
}

seal_err_t
SEAL_API
seal_get_src_queue_size(seal_src_t* src, size_t* psize)
{
    *psize = src->queue_size;

    return SEAL_OK;
}

seal_err_t
SEAL_API
seal_get_src_chunk_size(seal_src_t* src, size_t* psize)
{
    *psize = src->chunk_size;

    return SEAL_OK;
}

seal_err_t
SEAL_API
seal_get_src_pos(seal_src_t* src, float* px, float* py, float* pz)
{
    return get3f(src, AL_POSITION, px, py, pz);
}

seal_err_t
SEAL_API
seal_get_src_vel(seal_src_t* src, float* px, float* py, float* pz)
{
    return get3f(src, AL_VELOCITY, px, py, pz);
}

seal_err_t
SEAL_API
seal_get_src_pitch(seal_src_t* src, float* ppitch)
{
    return _seal_getf(src, AL_PITCH, ppitch, alGetSourcef);
}

seal_err_t
SEAL_API
seal_get_src_gain(seal_src_t* src, float* pgain)
{
    return _seal_getf(src, AL_GAIN, pgain, alGetSourcef);
}

seal_err_t
SEAL_API
seal_is_src_auto(seal_src_t* src, char* pauto)
{
    *pauto = src->automatic;

    return SEAL_OK;
}

seal_err_t
SEAL_API
seal_is_src_relative(seal_src_t* src, char* prelative)
{
    return _seal_getb(src, AL_SOURCE_RELATIVE, prelative, alGetSourcei);
}

seal_err_t
SEAL_API
seal_is_src_looping(seal_src_t* src, char* plooping)
{
    *plooping = src->looping;

    return SEAL_OK;
}

seal_err_t
SEAL_API
seal_get_src_type(seal_src_t* src, seal_src_type_t* ptype)
{
    int type;
    seal_err_t err;

    err = _seal_geti(src, AL_SOURCE_TYPE, &type, alGetSourcei);
    if (err == SEAL_OK) {
        switch (type) {
        case AL_STATIC:
            *ptype = SEAL_STATIC;
            break;
        case AL_STREAMING:
            *ptype = SEAL_STREAMING;
            break;
        default:
            *ptype = SEAL_UNDETERMINED;
        }
    }

    return err;
}

seal_err_t
SEAL_API
seal_get_src_state(seal_src_t* src, seal_src_state_t* pstate)
{
    int state;
    seal_err_t err;

    err = _seal_geti(src, AL_SOURCE_STATE, &state, alGetSourcei);
    if (err == SEAL_OK) {
        switch (state) {
        case AL_PLAYING:
            *pstate = SEAL_PLAYING;
            break;
        case AL_PAUSED:
            *pstate = SEAL_PAUSED;
            break;
        case AL_STOPPED:
            *pstate = SEAL_STOPPED;
            break;
        default:
            *pstate = SEAL_INITIAL;
        }
    }

    return err;
}
