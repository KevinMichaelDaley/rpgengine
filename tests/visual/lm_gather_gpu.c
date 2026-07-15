/**
 * @file lm_gather_gpu.c
 * @brief GPU GI gather compute kernel (rpg-8sv9), validated against analytic
 *        irradiance. One thread per luxel casts a stratified uniform-hemisphere
 *        of rays, sphere-traces the SDF, path-traces (emission + cosine bounce),
 *        escapes to sky, and accumulates SH9 with the CPU baker's exact
 *        convention (uniform weight 2pi/n^2, Y basis, Ramamoorthi eval).
 *
 * Analytic ground truth (isotropic environments):
 *   - open sky L                 -> irradiance ~= pi*L
 *   - enclosed black shell        -> ~= 0 (rays occluded, no light)
 *   - enclosed emissive shell E   -> ~= pi*E (emission fills the hemisphere)
 *   - open sky, tilted normal     -> ~= pi (isotropy)
 */
#include <glad/glad.h>
#include <SDL2/SDL.h>

#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#ifndef GL_COMPUTE_SHADER
#define GL_COMPUTE_SHADER 0x91B9
#endif
#ifndef GL_SHADER_STORAGE_BUFFER
#define GL_SHADER_STORAGE_BUFFER 0x90D2
#endif
#ifndef GL_SHADER_STORAGE_BARRIER_BIT
#define GL_SHADER_STORAGE_BARRIER_BIT 0x00002000
#endif
static void (*p_glDispatchCompute)(GLuint, GLuint, GLuint);
static void (*p_glMemoryBarrier)(GLbitfield);
static void (*p_glBindBufferBase)(GLenum, GLuint, GLuint);
#define glDispatchCompute p_glDispatchCompute
#define glMemoryBarrier p_glMemoryBarrier
#define glBindBufferBase p_glBindBufferBase

/* CPU SH irradiance eval, matching lm_sh9_irradiance exactly. */
static void sh_basis(float x, float y, float z, float o[9]) {
    o[0]=0.282094792f; o[1]=0.488602512f*y; o[2]=0.488602512f*z; o[3]=0.488602512f*x;
    o[4]=1.092548431f*x*y; o[5]=1.092548431f*y*z; o[6]=0.315391565f*(3.0f*z*z-1.0f);
    o[7]=1.092548431f*x*z; o[8]=0.546274215f*(x*x-y*y);
}
static float sh_irradiance(const float *c, float nx, float ny, float nz) {
    float y[9]; sh_basis(nx, ny, nz, y);
    const float A0=3.14159265f, A1=2.09439510f, A2=0.78539816f;
    return A0*c[0]*y[0] + A1*(c[1]*y[1]+c[2]*y[2]+c[3]*y[3])
         + A2*(c[4]*y[4]+c[5]*y[5]+c[6]*y[6]+c[7]*y[7]+c[8]*y[8]);
}

static GLuint make_compute(const char *src) {
    GLuint sh = glCreateShader(GL_COMPUTE_SHADER);
    glShaderSource(sh, 1, &src, NULL); glCompileShader(sh);
    GLint ok=0; glGetShaderiv(sh, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[4096]; glGetShaderInfoLog(sh, sizeof log, NULL, log);
        fprintf(stderr, "gather compile failed:\n%s\n", log); return 0; }
    GLuint p = glCreateProgram(); glAttachShader(p, sh); glLinkProgram(p); glDeleteShader(sh);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "gather link failed:\n%s\n", log); return 0; }
    return p;
}

static const char *CS_GATHER =
    "#version 430\n"
    "layout(local_size_x=64) in;\n"
    "layout(std430,binding=0) readonly buffer Sdf { float sdf[]; };\n"
    "layout(std430,binding=1) readonly buffer Mat { vec4 mat[]; };\n"   /* [2i]=diffuse,[2i+1]=emissive */
    "layout(std430,binding=2) readonly buffer Lux { vec4 lux[]; };\n"   /* [2i]=pos,[2i+1]=normal */
    "layout(std430,binding=3) writeonly buffer Out { float osh[]; };\n"
    "uniform ivec3 dims; uniform float voxel; uniform vec3 origin;\n"
    "uniform vec3 sky; uniform int samples; uniform int bounces; uniform uint seed; uniform int nlux;\n"
    "uint pcg(inout uint s){ s=s*747796405u+2891336453u; uint w=((s>>((s>>28)+4u))^s)*277803737u; return (w>>22)^w; }\n"
    "float rnd(inout uint s){ return float(pcg(s))*(1.0/4294967296.0); }\n"
    "int cidx(ivec3 c){ return (c.z*dims.y+c.y)*dims.x+c.x; }\n"
    "bool inb(ivec3 c){ return all(greaterThanEqual(c,ivec3(0)))&&all(lessThan(c,dims)); }\n"
    "ivec3 toCell(vec3 p){ return ivec3(floor((p-origin)/voxel)); }\n"
    "float sSDF(vec3 p){ ivec3 c=toCell(p); if(!inb(c)) return 1e9; return sdf[cidx(c)]; }\n"
    "void basis(vec3 n, out vec3 t, out vec3 b){ vec3 u=abs(n.z)<0.999?vec3(0,0,1):vec3(1,0,0); t=normalize(cross(u,n)); b=cross(n,t); }\n"
    "void shb(vec3 d, out float y[9]){ float x=d.x,yy=d.y,z=d.z;\n"
    "  y[0]=0.282094792; y[1]=0.488602512*yy; y[2]=0.488602512*z; y[3]=0.488602512*x;\n"
    "  y[4]=1.092548431*x*yy; y[5]=1.092548431*yy*z; y[6]=0.315391565*(3.0*z*z-1.0);\n"
    "  y[7]=1.092548431*x*z; y[8]=0.546274215*(x*x-yy*yy); }\n"
    /* Sphere-trace the SDF; near the surface (< 1.5 voxel) step finely to the hit. */
    "bool trace(vec3 o, vec3 dir, out vec3 hp, out ivec3 hc, out vec3 hn){\n"
    "  float t=voxel;\n"
    "  for(int i=0;i<512;++i){ vec3 p=o+dir*t; ivec3 c=toCell(p); if(!inb(c)) return false;\n"
    "    float d=sdf[cidx(c)];\n"
    "    if(d < voxel*1.5){ for(int j=0;j<16;++j){ t+=voxel*0.25; p=o+dir*t; c=toCell(p); if(!inb(c)) return false;\n"
    "        if(sdf[cidx(c)]<0.0){ hp=p; hc=c; float e=voxel;\n"
    "          vec3 g=vec3(sSDF(p+vec3(e,0,0))-sSDF(p-vec3(e,0,0)), sSDF(p+vec3(0,e,0))-sSDF(p-vec3(0,e,0)), sSDF(p+vec3(0,0,e))-sSDF(p-vec3(0,0,e)));\n"
    "          hn=normalize(g); return true; } }\n"
    "      return false; }\n"
    "    t += max(d, voxel); if(t>1e4) return false; }\n"
    "  return false; }\n"
    "void main(){ uint li=gl_GlobalInvocationID.x; if(li>=uint(nlux)) return;\n"
    "  vec3 pos=lux[2u*li].xyz; vec3 nrm=normalize(lux[2u*li+1u].xyz);\n"
    "  vec3 tn,bt; basis(nrm,tn,bt);\n"
    "  int n=int(sqrt(float(samples))); if(n<1) n=1; float weight=6.28318530718/float(n*n);\n"
    "  float sh[27]; for(int k=0;k<27;++k) sh[k]=0.0;\n"
    "  uint rs = seed ^ (li*2654435761u);\n"
    "  for(int sy=0;sy<n;++sy) for(int sx=0;sx<n;++sx){\n"
    "    float u1=(float(sx)+rnd(rs))/float(n); float u2=(float(sy)+rnd(rs))/float(n);\n"
    "    float z=u1; float r=sqrt(max(1.0-z*z,0.0)); float phi=6.28318530718*u2;\n"
    "    vec3 dir=tn*(r*cos(phi))+bt*(r*sin(phi))+nrm*z;\n"
    "    vec3 Li=vec3(0.0), thr=vec3(1.0); vec3 o=pos+nrm*voxel*1.5, d=dir;\n"
    "    for(int b=0;b<=bounces;++b){ vec3 hp; ivec3 hc; vec3 hn;\n"
    "      if(!trace(o,d,hp,hc,hn)){ Li+=thr*sky; break; }\n"
    "      Li += thr*mat[2*cidx(hc)+1].rgb; thr *= mat[2*cidx(hc)].rgb;\n"
    "      if(b==bounces) break;\n"
    "      vec3 ct,cb; basis(hn,ct,cb); float a=rnd(rs),e2=rnd(rs);\n"
    "      float cz=sqrt(a), cr=sqrt(1.0-a), cp=6.28318530718*e2;\n"
    "      d=ct*(cr*cos(cp))+cb*(cr*sin(cp))+hn*cz; o=hp+hn*voxel*1.5; }\n"
    "    float y[9]; shb(normalize(dir),y);\n"
    "    for(int c=0;c<3;++c) for(int i=0;i<9;++i) sh[c*9+i]+=Li[c]*weight*y[i]; }\n"
    "  for(int k=0;k<27;++k) osh[27u*li+uint(k)]=sh[k]; }\n";

/* sdBox: negative inside the box [lo,hi], positive outside. */
static float sd_box(float px,float py,float pz,const float lo[3],const float hi[3]){
    float cx=(lo[0]+hi[0])*0.5f,cy=(lo[1]+hi[1])*0.5f,cz=(lo[2]+hi[2])*0.5f;
    float hx=(hi[0]-lo[0])*0.5f,hy=(hi[1]-lo[1])*0.5f,hz=(hi[2]-lo[2])*0.5f;
    float qx=fabsf(px-cx)-hx,qy=fabsf(py-cy)-hy,qz=fabsf(pz-cz)-hz;
    float ox=qx>0?qx:0,oy=qy>0?qy:0,oz=qz>0?qz:0;
    float out=sqrtf(ox*ox+oy*oy+oz*oz);
    float in=fmaxf(qx,fmaxf(qy,qz)); if(in>0) in=0;
    return out+in;
}

int main(void){
    if(SDL_Init(SDL_INIT_VIDEO)!=0){ fprintf(stderr,"SDL init failed\n"); return 1; }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION,4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION,3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK,SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_Window *win=SDL_CreateWindow("gather",0,0,64,64,SDL_WINDOW_OPENGL|SDL_WINDOW_HIDDEN);
    SDL_GLContext gc=SDL_GL_CreateContext(win); SDL_GL_MakeCurrent(win,gc);
    if(!gladLoadGLLoader((GLADloadproc)SDL_GL_GetProcAddress)){ fprintf(stderr,"glad failed\n"); return 1; }
    p_glDispatchCompute=(void(*)(GLuint,GLuint,GLuint))SDL_GL_GetProcAddress("glDispatchCompute");
    p_glMemoryBarrier=(void(*)(GLbitfield))SDL_GL_GetProcAddress("glMemoryBarrier");
    p_glBindBufferBase=(void(*)(GLenum,GLuint,GLuint))SDL_GL_GetProcAddress("glBindBufferBase");
    if(!p_glDispatchCompute||!p_glMemoryBarrier||!p_glBindBufferBase){ fprintf(stderr,"no compute\n"); return 1; }
    printf("GL_RENDERER: %s\n",(const char*)glGetString(GL_RENDERER));

    GLuint prog=make_compute(CS_GATHER); if(!prog) return 1;

    const int N=48; const float voxel=1.0f; const float origin[3]={0,0,0};
    const size_t cells=(size_t)N*N*N;
    const float lo[3]={8,8,8}, hi[3]={40,40,40};            /* air pocket. */
    float *sdf=malloc(cells*sizeof(float));
    float *mat=malloc(cells*2*4*sizeof(float));             /* diffuse,emissive vec4 pairs. */
    /* One luxel at the pocket centre; normal chosen per test below. */
    float lux[8]={24.5f,24.5f,24.5f,0, 0,0,1,0};
    GLuint bo_sdf,bo_mat,bo_lux,bo_out;
    glGenBuffers(1,&bo_sdf); glGenBuffers(1,&bo_mat); glGenBuffers(1,&bo_lux); glGenBuffers(1,&bo_out);
    float outsh[27];

    int dims[3]={N,N,N};
    #define RUN(sky_,bounces_,nx,ny,nz) do{ \
        lux[4]=nx; lux[5]=ny; lux[6]=nz; \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_sdf); glBufferData(GL_SHADER_STORAGE_BUFFER,cells*sizeof(float),sdf,GL_STATIC_DRAW); \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_mat); glBufferData(GL_SHADER_STORAGE_BUFFER,cells*2*4*sizeof(float),mat,GL_STATIC_DRAW); \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_lux); glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof lux,lux,GL_STATIC_DRAW); \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_out); glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof outsh,NULL,GL_DYNAMIC_COPY); \
        glUseProgram(prog); \
        glUniform3iv(glGetUniformLocation(prog,"dims"),1,dims); \
        glUniform1f(glGetUniformLocation(prog,"voxel"),voxel); \
        glUniform3fv(glGetUniformLocation(prog,"origin"),1,origin); \
        glUniform3f(glGetUniformLocation(prog,"sky"),sky_,sky_,sky_); \
        glUniform1i(glGetUniformLocation(prog,"samples"),1024); \
        glUniform1i(glGetUniformLocation(prog,"bounces"),bounces_); \
        glUniform1ui(glGetUniformLocation(prog,"seed"),12345u); \
        glUniform1i(glGetUniformLocation(prog,"nlux"),1); \
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,bo_sdf); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,bo_mat); \
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,bo_lux); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,bo_out); \
        glDispatchCompute(1,1,1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_out); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof outsh,outsh); \
    }while(0)

    /* --- Test 1: open sky=1 (no occluders) -> irradiance ~ pi --- */
    for(size_t i=0;i<cells;++i) sdf[i]=1000.0f;
    for(size_t i=0;i<cells*8;++i) mat[i]=0.0f;
    RUN(1.0f,0,0,0,1);
    float e_open = sh_irradiance(outsh,0,0,1);

    /* --- Test 2: open sky=1, tilted normal -> ~ pi (isotropy) --- */
    float nx=0.3f,ny=0.6f,nz=0.74f; float nl=sqrtf(nx*nx+ny*ny+nz*nz); nx/=nl;ny/=nl;nz/=nl;
    RUN(1.0f,0,nx,ny,nz);
    float e_tilt = sh_irradiance(outsh,nx,ny,nz);

    /* --- Build the enclosed shell SDF (air pocket, solid outside). --- */
    for(int z=0;z<N;++z)for(int y=0;y<N;++y)for(int x=0;x<N;++x){
        float b=sd_box((float)x+0.5f,(float)y+0.5f,(float)z+0.5f,lo,hi);
        sdf[(size_t)(z*N+y)*N+x]=-b; /* negative in solid (outside box), positive in air. */
    }
    /* Test 3: enclosed black shell -> ~ 0 (occluded). */
    for(size_t i=0;i<cells*8;++i) mat[i]=0.0f;
    RUN(0.0f,0,0,0,1);
    float e_black = sh_irradiance(outsh,0,0,1);

    /* Test 4: enclosed emissive shell E=1 -> ~ pi (emission fills the hemisphere). */
    for(size_t i=0;i<cells;++i){ mat[i*8+4]=1.0f; mat[i*8+5]=1.0f; mat[i*8+6]=1.0f; } /* emissive rgb=1. */
    RUN(0.0f,0,0,0,1);
    float e_emis = sh_irradiance(outsh,0,0,1);

    const float PI=3.14159265f;
    printf("open(sky=1)     E=%.3f  (expect ~pi=%.3f)\n", e_open, PI);
    printf("open tilted N   E=%.3f  (expect ~pi)\n", e_tilt);
    printf("enclosed black  E=%.3f  (expect ~0)\n", e_black);
    printf("enclosed emis=1 E=%.3f  (expect ~pi)\n", e_emis);

    int fail = 0;
    if (fabsf(e_open - PI) > 0.35f) fail |= 1;     /* SH truncation + MC. */
    if (fabsf(e_tilt - e_open) > 0.25f) fail |= 2; /* isotropy. */
    if (fabsf(e_black) > 0.20f) fail |= 4;         /* occluded. */
    if (fabsf(e_emis - PI) > 0.40f) fail |= 8;     /* emission = sky. */
    printf("%s%s\n", fail ? "FAILED mask=" : "PASSED", fail ? "" : "");
    if (fail) printf("fail mask %d\n", fail);

    free(sdf); free(mat);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return fail ? 1 : 0;
}
