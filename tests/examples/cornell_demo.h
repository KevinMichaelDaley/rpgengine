/**
 * @file cornell_demo.h
 * @brief Self-contained Cornell-box GI demo for the demo client (--cornell).
 *
 * Builds a Cornell box, bakes a radiosity lightmap with the offline baker
 * (src/lightmap), uploads the atlas, and renders the box with a lightmap shader
 * in its own SDL/GL window -- no server connection. Optionally writes a PPM
 * screenshot. Returns 0 on success, non-zero on failure.
 */
#ifndef FERRUM_CORNELL_DEMO_H
#define FERRUM_CORNELL_DEMO_H

/**
 * @brief Run the Cornell-box lightmap demo.
 * @param screenshot_path  PPM path to write after the first rendered frame
 *                         (NULL to skip).
 * @param seconds          How long to keep the window open (0 = one frame).
 */
int cornell_demo_run(const char *screenshot_path, double seconds);

#endif /* FERRUM_CORNELL_DEMO_H */
