#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ferrum/procgen/procgen_level_load.h"
#include "ferrum/procgen/procgen_tokenize.h"
#include "ferrum/procgen/grammar_blockout.h"

int main(int argc, char **argv) {
    if (argc < 2) { fprintf(stderr, "Usage: %s <tokenfile.txt> [output.obj]\n", argv[0]); return 1; }
    const char *infile = argv[1];
    const char *outfile = argc >= 3 ? argv[2] : "/tmp/procgen_output.obj";

    procgen_level_t lvl;
    procgen_level_init(&lvl);
    if (procgen_level_load(&lvl, infile) != 0) {
        fprintf(stderr, "Failed to load level\n");
        return 1;
    }

    printf("Writing OBJ: %u triangles to %s\n",
           lvl.mesh.vertex_count / 18 * 2, outfile);

    FILE *f = fopen(outfile, "w");
    if (!f) { perror(outfile); return 1; }

    fprintf(f, "# Procgen level mesh\n");
    fprintf(f, "o procgen_dungeon\n");

    /* Vertices */
    uint32_t vc = lvl.mesh.vertex_count;
    for (uint32_t i = 0; i < vc; i += 3)
        fprintf(f, "v %f %f %f\n",
                lvl.mesh.vertices[i], lvl.mesh.vertices[i+1], lvl.mesh.vertices[i+2]);

    /* Faces (triangles) */
    for (uint32_t i = 0; i < vc; i += 9) {
        /* OBJ indices are 1-based */
        uint32_t i1 = i/3 + 1, i2 = i/3 + 2, i3 = i/3 + 3;
        fprintf(f, "f %u %u %u\n", i1, i2, i3);
    }
    fclose(f);
    procgen_level_free(&lvl);
    printf("Done: %s\n", outfile);
    return 0;
}
