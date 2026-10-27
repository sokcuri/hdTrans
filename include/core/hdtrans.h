/* C API of the hdtrans engine. Strings are UTF-8. Buffers returned through an out
 * parameter are released with hdTrans_FreeMem. One engine and one session per process.
 * Each call is translated on its own: nothing from earlier calls shapes it.
 * Calls are thread-safe, not reentrant; finish all calls before unloading the library. */
#ifndef HDTRANS_H
#define HDTRANS_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#if defined(_WIN32) && defined(HDTRANS_BUILD_DLL)
#define HDTRANS_API __declspec(dllexport)
#elif defined(_WIN32) && defined(HDTRANS_USE_DLL)
#define HDTRANS_API __declspec(dllimport)
#else
#define HDTRANS_API
#endif

/* Loads dirs (NUL-separated, double NUL at the end), later folders overriding earlier
 * ones. Empty dirs uses the config's dicts. The data root is hdTrans/ next to the library
 * when present, otherwise the library folder; user/ holds config and userdict*.txt/tsv.
 * Legacy files next to the library remain supported. Ehnd is opt-in through config
 * or hdTrans_OpenEhnd. User dictionary errors are available through hdTrans_UserDictError.
 * Changed user files reload in the background, except on WebAssembly.
 * Returns 0, or -1 when no dictionary loads or a concurrent Terminate, successful
 * Initialize or successful Reload cancels this load. */
HDTRANS_API int hdTrans_Initialize(const char* dirs);

typedef struct hdTrans_Options {
    uint32_t size;
    uint32_t strict_dict; /* 0: config/default, 1: strict, 2: skip bad rows */
} hdTrans_Options;

/* Sets options for following dictionary loads, including before Initialize, without
 * canceling a load already in progress. Terminate resets these options.
 * NULL or size 0 resets defaults; size 1..3 is invalid. Fields absent from size
 * use defaults; future trailing fields are ignored. Returns 0 or -1 for invalid
 * size/values, leaving the options unchanged on error. */
HDTRANS_API int hdTrans_SetOptions(const hdTrans_Options* options);

/* Releases the session and dictionaries and resets options. Repeated calls are harmless. */
HDTRANS_API void hdTrans_Terminate(void);

/* Translates text[0..n), one or more "\n"-separated lines, in the process session.
 * Writes a NUL-terminated UTF-8 buffer to *out and its length (without the NUL) to
 * *outn. Returns 0, or -1 when uninitialized or terminated. */
HDTRANS_API int hdTrans_Translate(const char* text, size_t n, char** out, size_t* outn);

/* Translates text[0..n) as the lines that follow context[0..cn), "\n"-separated lines of
 * the same document: the context gives its cues (names, speech, quotes, senses) and, with
 * wrapped set in the config, the start of a sentence text goes on with. Only the lines of
 * text are written, as by hdTrans_Translate. NULL or cn 0 is no context. Returns 0, or -1
 * when uninitialized or terminated. */
HDTRANS_API int hdTrans_TranslateContext(const char* context, size_t cn, const char* text, size_t n,
                                         char** out, size_t* outn);

/* Takes the engine's current dictionaries into the session. A session keeps nothing from
 * earlier calls, so translations are unchanged otherwise. Returns 0, or -1 when
 * uninitialized or terminated. */
HDTRANS_API int hdTrans_Reset(void);

/* Rereads config, dictionary folders and the user dictionary with stored options.
 * Returns 0, or -1 when uninitialized, terminated, or the reload fails; a failed
 * load keeps the dictionaries loaded before. */
HDTRANS_API int hdTrans_Reload(void);

/* Writes config and user dictionary diagnostics, one line per item, as
 * a buffer like hdTrans_Translate does; empty when there are none. Returns 0,
 * or -1 before initialization when no failed load diagnostic is available. */
HDTRANS_API int hdTrans_UserDictError(char** out, size_t* outn);

/* Takes the filters and user dictionary of the Ehnd folder dir (UTF-8; NULL or empty
 * drops them) for the following translations; dir is watched and reloaded like
 * the user dictionary files. Returns 0, or -1 when uninitialized, terminated, or dir cannot be read. */
HDTRANS_API int hdTrans_OpenEhnd(const char* dir);

/* Writes the lines of the filter files and of the Ehnd folder that were skipped, as
 * "file:line: message" lines, as a buffer like hdTrans_Translate does. Returns 0, or -1
 * before initialization when no failed load diagnostic is available. */
HDTRANS_API int hdTrans_FilterErrors(char** out, size_t* outn);

HDTRANS_API void hdTrans_FreeMem(void* p);

/* Version string of the library, e.g. "0.1.0". */
HDTRANS_API const char* hdTrans_Version(void);

#ifdef __cplusplus
}
#endif

#endif
