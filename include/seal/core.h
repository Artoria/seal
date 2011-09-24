/*
 * core.h is part of the Scorched End Audio Library (SEAL) and is licensed
 * under the terms of the GNU Lesser General Public License. See COPYING
 * attached with the library.
 *
 * core.h defines interfaces for low-level SEAL operations.
 */

#ifndef _SEAL_CORE_H_
#define _SEAL_CORE_H_

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Gets the SEAL version string. */
const char* seal_get_verion(void);

/*
 * Initializes SEAL by specifying the device name. This function is not
 * re-entrant nor thread-safe and should be called only once per SEAL session.
 * Match a call to `seal_startup' with a call to `seal_cleanup' and never call
 * `seal_starup' twice in a row.
 *
 * @param device_name   the name of a device; 0 to use the default one
 * @return              nonzero if successful or otherwise 0
 */
int seal_startup(const char* /*device_name*/);

/* Uninitializes SEAL and invalidate all SEAL objects. Thread-unsafe. */
void seal_cleanup(void);

#ifdef __cplusplus
}
#endif

/* IMPLEMENTATION DETAILS STARTS FROM HERE. */

/*
 * OpenAL's error state is not thread-safe and so semaphores are needed unless
 * SEAL_NO_THREAD_SAFETY is defined. Ideally we should lock only before
 * setting the error and unlock after retrieving the error, but that requires
 * a modification to the OpenAL implementation being used... */
void _seal_lock_al(void);
void _seal_unlock_al(void);

/* Memory operations. */
void* _seal_malloc(size_t);
void* _seal_calloc(size_t /*count*/, size_t /*size*/);
void* _seal_realloc(void* /*mem*/, size_t);
void _seal_free(void* /*mem*/);

/* Interfaces for working with dynamic libraries. */
void* _seal_open_lib(const char* /*lib_filename*/);
void* _seal_get_sym(void* /*lib*/, const char* /*symbol*/);
void _seal_close_lib(void* /*lib*/);

#endif /* _SEAL_CORE_H_ */
