/**
 * @file fskel_format.h
 * @brief .fskel format constants.
 *
 * The fskel format is JSON (version 5+).  Previous binary versions
 * (1-4) are no longer supported by the loader.
 *
 * Public types: 0
 * Public functions: 0
 */

#ifndef FERRUM_ANIMATION_FSKEL_FORMAT_H
#define FERRUM_ANIMATION_FSKEL_FORMAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @brief Current format version (JSON). */
#define FSKEL_VERSION 5u

#ifdef __cplusplus
}
#endif

#endif /* FERRUM_ANIMATION_FSKEL_FORMAT_H */
