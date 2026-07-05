/**
 * @file procgen_viewer.c
 * @brief Minimal OBJ viewer for procgen dungeon meshes.
 *        WASD+mouse to fly. No server needed.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include <SDL2/SDL.h>
#include <GL/gl.h>

typedef struct { float x,y,z; } v3;
static v3 cam_pos = {0,10,20};
static float cam_yaw=0, cam_pitch=-0.3f;
static int g_w=1280, g_h=720;
static float *g_verts=NULL;
static int g_vc=0, g_fc=0;

static int load_obj(const char *path) {
    FILE *f = fopen(path,"r");
    if(!f) return -1;
    g_vc=0; g_fc=0;
    float *tmpv = malloc(300000*sizeof(float));
    int *tmpf = malloc(100000*3*sizeof(int));
    if(!tmpv||!tmpf){fclose(f);return -1;}
    char line[256];
    while(fgets(line,sizeof(line),f)) {
        if(line[0]=='v'&&line[1]==' ') {
            float x,y,z;
            if(sscanf(line,"v %f %f %f",&x,&y,&z)==3&&g_vc<299997) {
                tmpv[g_vc++]=x;tmpv[g_vc++]=y;tmpv[g_vc++]=z;
            }
        } else if(line[0]=='f'&&line[1]==' ') {
            int a,b,c;
            if(sscanf(line,"f %d %d %d",&a,&b,&c)==3&&g_fc<99997) {
                tmpf[g_fc*3+0]=a-1;tmpf[g_fc*3+1]=b-1;tmpf[g_fc*3+2]=c-1;g_fc++;
            }
        }
    }
    fclose(f);
    /* Interleave vertices */
    g_verts = malloc((size_t)g_fc*9*sizeof(float));
    if(!g_verts){free(tmpv);free(tmpf);return -1;}
    for(int i=0;i<g_fc;i++) {
        int a=tmpf[i*3],b=tmpf[i*3+1],c=tmpf[i*3+2];
        memcpy(g_verts+i*9+0,tmpv+a*3,9);memcpy(g_verts+i*9+3,tmpv+b*3,9);memcpy(g_verts+i*9+6,tmpv+c*3,9);
    }
    free(tmpv);free(tmpf);
    printf("Loaded %d faces\n",g_fc);
    return 0;
}

static void look_at(v3 eye, v3 center, v3 up) {
    glLoadIdentity();
    gluLookAt(eye.x,eye.y,eye.z, center.x,center.y,center.z, up.x,up.y,up.z);
}

int main(int argc, char **argv) {
    if(argc<2){fprintf(stderr,"Usage: %s file.obj\n",argv[0]);return 1;}
    if(load_obj(argv[1])!=0){fprintf(stderr,"Failed to load %s\n",argv[1]);return 1;}

    SDL_Init(SDL_INIT_VIDEO);
    SDL_Window *win = SDL_CreateWindow("Procgen Viewer", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, g_w, g_h, SDL_WINDOW_OPENGL);
    SDL_GLContext ctx = SDL_GL_CreateContext(win);
    SDL_SetRelativeMouseMode(SDL_TRUE);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_LIGHTING);
    glEnable(GL_LIGHT0);
    glEnable(GL_COLOR_MATERIAL);
    glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
    float lp[]={0.5f,0.8f,1.0f,0}, la[]={0.2f,0.2f,0.2f,1}, ld[]={0.8f,0.8f,0.8f,1};
    glLightfv(GL_LIGHT0,GL_POSITION,lp); glLightfv(GL_LIGHT0,GL_AMBIENT,la); glLightfv(GL_LIGHT0,GL_DIFFUSE,ld);
    glClearColor(0.05f,0.08f,0.12f,1);

    int running=1;
    while(running) {
        SDL_Event e;
        while(SDL_PollEvent(&e)) {
            if(e.type==SDL_QUIT||(e.type==SDL_KEYDOWN&&e.key.keysym.sym==SDLK_ESCAPE)) running=0;
            if(e.type==SDL_MOUSEMOTION){cam_yaw+=e.motion.xrel*0.003f;cam_pitch-=e.motion.yrel*0.003f;if(cam_pitch<-1.5f)cam_pitch=-1.5f;if(cam_pitch>1.5f)cam_pitch=1.5f;}
        }
        const Uint8 *k=SDL_GetKeyboardState(NULL);
        float speed=0.15f, fx=cosf(cam_yaw), fz=sinf(cam_yaw), rx=-fz, rz=fx;
        if(k[SDL_SCANCODE_W]){cam_pos.x+=fx*speed;cam_pos.z+=fz*speed;}
        if(k[SDL_SCANCODE_S]){cam_pos.x-=fx*speed;cam_pos.z-=fz*speed;}
        if(k[SDL_SCANCODE_A]){cam_pos.x+=rx*speed;cam_pos.z+=rz*speed;}
        if(k[SDL_SCANCODE_D]){cam_pos.x-=rx*speed;cam_pos.z-=rz*speed;}
        if(k[SDL_SCANCODE_SPACE]) cam_pos.y+=speed;
        if(k[SDL_SCANCODE_LSHIFT]) cam_pos.y-=speed;

        glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
        glMatrixMode(GL_PROJECTION);
        glLoadIdentity();
        gluPerspective(70,(double)g_w/g_h,0.1,500);
        glMatrixMode(GL_MODELVIEW);
        v3 up={0,1,0}, fwd={cosf(cam_pitch)*fx,sinf(cam_pitch),cosf(cam_pitch)*fz};
        look_at(cam_pos,(v3){cam_pos.x+fwd.x,cam_pos.y+fwd.y,cam_pos.z+fwd.z},up);

        /* Grid floor */
        glColor3f(0.2f,0.22f,0.25f);
        glBegin(GL_LINES);
        for(int i=-40;i<=40;i++){glVertex3f(i,0,-40);glVertex3f(i,0,40);glVertex3f(-40,0,i);glVertex3f(40,0,i);}
        glEnd();

        /* Solid mesh with lighting */
        glColor3f(0.6f, 0.55f, 0.5f);
        glEnableClientState(GL_VERTEX_ARRAY);
        glEnableClientState(GL_NORMAL_ARRAY);
        glVertexPointer(3,GL_FLOAT,0,g_verts);
        /* Normals: per-face, every 3 vertices form a triangle */
        float *norms = malloc((size_t)g_fc*9*sizeof(float));
        for(int i=0;i<g_fc;i++) {
            float *a=g_verts+i*9, *b=g_verts+i*9+3, *c=g_verts+i*9+6;
            float ux=b[0]-a[0],uy=b[1]-a[1],uz=b[2]-a[2];
            float vx=c[0]-a[0],vy=c[1]-a[1],vz=c[2]-a[2];
            float nx=uy*vz-uz*vy, ny=uz*vx-ux*vz, nz=ux*vy-uy*vx;
            float len=sqrtf(nx*nx+ny*ny+nz*nz);
            if(len>0){nx/=len;ny/=len;nz/=len;}
            for(int j=0;j<3;j++){norms[i*9+j*3]=nx;norms[i*9+j*3+1]=ny;norms[i*9+j*3+2]=nz;}
        }
        glNormalPointer(GL_FLOAT,0,norms);
        glDrawArrays(GL_TRIANGLES,0,g_fc*3);
        glDisableClientState(GL_NORMAL_ARRAY);
        glDisableClientState(GL_VERTEX_ARRAY);
        free(norms);

        SDL_GL_SwapWindow(win);
        SDL_Delay(5);
    }
    free(g_verts);
    SDL_GL_DeleteContext(ctx);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
