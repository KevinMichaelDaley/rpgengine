/**
 * @file lm_gather_gpu.c
 * @brief GPU GI gather compute kernel (rpg-8sv9): sphere-traced, multi-bounce,
 *        all analytic light types (sun/point/spot) + emissive voxels, validated
 *        against analytic ground truth. Matches the CPU baker's convention
 *        (lm_gi_trace / lm_gi_direct / lm_sh9): uniform-hemisphere weight
 *        2pi/n^2, per-hit  L += thr*(emissive + diffuse*E/pi),  direct E = sum
 *        of visibility*cosNL*atten*colour, cosine bounce with throughput*=diffuse.
 *
 * Validations (Iris Xe):
 *   - open sky L                       -> irradiance ~= pi*L
 *   - enclosed black shell              -> ~= 0 (sphere-traced occlusion)
 *   - enclosed emissive shell E, rho    -> pi*E*(1+rho+..+rho^bounces) (MULTI-BOUNCE)
 *   - direct probe: directional/point/spot -> analytic cosNL*atten*colour
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
        fprintf(stderr, "compile failed:\n%s\n", log); return 0; }
    GLuint p = glCreateProgram(); glAttachShader(p, sh); glLinkProgram(p); glDeleteShader(sh);
    glGetProgramiv(p, GL_LINK_STATUS, &ok);
    if (!ok) { char log[4096]; glGetProgramInfoLog(p, sizeof log, NULL, log);
        fprintf(stderr, "link failed:\n%s\n", log); return 0; }
    return p;
}

static const char *CS =
    "#version 430\n"
    "layout(local_size_x=64) in;\n"
    "layout(std430,binding=0) readonly buffer Sdf { float sdf[]; };\n"
    "layout(std430,binding=1) readonly buffer Mat { vec4 mat[]; };\n"    /* [2i]=diffuse,[2i+1]=emissive */
    "layout(std430,binding=2) readonly buffer Lux { vec4 lux[]; };\n"    /* [2i]=pos,[2i+1]=normal */
    "layout(std430,binding=3) writeonly buffer Out { float osh[]; };\n"
    "layout(std430,binding=4) readonly buffer Lit { vec4 lit[]; };\n"    /* 4 vec4 per light */
    "struct Node { uint children[8]; vec4 diffuse; vec4 emissive; };\n"
    "layout(std430,binding=5) readonly buffer Svo { Node nodes[]; };\n"
    "uniform ivec3 dims; uniform float voxel; uniform vec3 origin;\n"
    "uniform vec3 sky; uniform int samples; uniform int bounces; uniform uint seed;\n"
    "uniform int nlux; uniform int nlights; uniform int mode;\n"
    "uniform vec3 svomin; uniform vec3 svomax; uniform int svodepth; uniform int usesvo;\n"
    "const float PI=3.14159265, TAU=6.28318530718, INVPI=0.31830988618;\n"
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
    /* Sphere-trace to `maxT`. Returns hit (< maxT) with pos/cell/normal. */
    "bool trace(vec3 o, vec3 dir, float maxT, out vec3 hp, out ivec3 hc, out vec3 hn){\n"
    "  float t=voxel;\n"
    "  for(int i=0;i<512 && t<maxT;++i){ vec3 p=o+dir*t; ivec3 c=toCell(p); if(!inb(c)) return false;\n"
    "    float d=sdf[cidx(c)];\n"
    "    if(d < voxel*1.5){ for(int j=0;j<16 && t<maxT;++j){ t+=voxel*0.25; p=o+dir*t; c=toCell(p); if(!inb(c)) return false;\n"
    "        if(sdf[cidx(c)]<0.0){ hp=p; hc=c; float e=voxel;\n"
    "          vec3 g=vec3(sSDF(p+vec3(e,0,0))-sSDF(p-vec3(e,0,0)), sSDF(p+vec3(0,e,0))-sSDF(p-vec3(0,e,0)), sSDF(p+vec3(0,0,e))-sSDF(p-vec3(0,0,e)));\n"
    "          hn=normalize(g); return true; } }\n"
    "      return false; }\n"
    "    t += max(d, voxel); }\n"
    "  return false; }\n"
    "bool shadow(vec3 o, vec3 dir, float maxT){ vec3 hp; ivec3 hc; vec3 hn; return trace(o,dir,maxT,hp,hc,hn); }\n"
    /* Direct irradiance from all analytic lights (matches lm_gi_direct + spot cone). */
    "vec3 direct(vec3 p, vec3 n){ vec3 e=vec3(0); vec3 o=p+n*voxel*1.5;\n"
    "  for(int i=0;i<nlights;++i){ vec3 lp=lit[4*i].xyz; int kind=int(lit[4*i].w);\n"
    "    vec3 ld=lit[4*i+1].xyz; vec3 col=lit[4*i+2].xyz; float ci=lit[4*i+2].w; float co=lit[4*i+3].x;\n"
    "    vec3 toL; float atten=1.0; float md;\n"
    "    if(kind==1){ toL=normalize(-ld); md=1e5; }\n"
    "    else { vec3 dd=lp-p; float dist=length(dd); if(dist<1e-4) continue; toL=dd/dist; atten=1.0/(dist*dist); md=dist-voxel*1.5; }\n"
    "    float cosNL=dot(n,toL); if(cosNL<=0.0) continue;\n"
    "    if(kind==2){ float ca=dot(-toL,normalize(ld)); float sp=clamp((ca-co)/max(ci-co,1e-4),0.0,1.0); atten*=sp; if(atten<=0.0) continue; }\n"
    "    if(shadow(o,toL,md)) continue;\n"
    "    e += col*(cosNL*atten); }\n"
    "  return e; }\n"
    /* Refine into the SVO octree at a surface point p (SDF already found it):\n"
     * descend children[(cz<<2)|(cy<<1)|cx] to the finest node, read its material. */
    "void svoMat(vec3 p, out vec3 diff, out vec3 emis){ diff=vec3(0); emis=vec3(0);\n"
    "  if(any(lessThan(p,svomin))||any(greaterThan(p,svomax))) return;\n"
    "  uint node=0u; vec3 lo=svomin, hi=svomax;\n"
    "  for(int d=0; d<svodepth; ++d){ vec3 mid=(lo+hi)*0.5;\n"
    "    uint cx=p.x>=mid.x?1u:0u, cy=p.y>=mid.y?1u:0u, cz=p.z>=mid.z?1u:0u;\n"
    "    uint child=nodes[node].children[(cz<<2)|(cy<<1)|cx];\n"
    "    if(child==0xFFFFFFFFu) break;\n"
    "    if(cx==1u) lo.x=mid.x; else hi.x=mid.x;\n"
    "    if(cy==1u) lo.y=mid.y; else hi.y=mid.y;\n"
    "    if(cz==1u) lo.z=mid.z; else hi.z=mid.z; node=child; }\n"
    "  diff=nodes[node].diffuse.rgb; emis=nodes[node].emissive.rgb; }\n"
    "void main(){ uint li=gl_GlobalInvocationID.x; if(li>=uint(nlux)) return;\n"
    "  vec3 pos=lux[2u*li].xyz; vec3 nrm=normalize(lux[2u*li+1u].xyz);\n"
    "  if(mode==1){ vec3 e=direct(pos,nrm); osh[27u*li]=e.x; osh[27u*li+1u]=e.y; osh[27u*li+2u]=e.z; return; }\n"
    "  if(mode==2){ vec3 df,em; svoMat(pos,df,em); osh[27u*li]=df.x; osh[27u*li+1u]=df.y; osh[27u*li+2u]=df.z;\n"
    "    osh[27u*li+3u]=em.x; osh[27u*li+4u]=em.y; osh[27u*li+5u]=em.z; return; }\n"
    "  vec3 tn,bt; basis(nrm,tn,bt);\n"
    "  int n=int(sqrt(float(samples))); if(n<1) n=1; float weight=TAU/float(n*n);\n"
    "  float sh[27]; for(int k=0;k<27;++k) sh[k]=0.0;\n"
    "  uint rs = seed ^ (li*2654435761u);\n"
    "  for(int sy=0;sy<n;++sy) for(int sx=0;sx<n;++sx){\n"
    "    float u1=(float(sx)+rnd(rs))/float(n); float u2=(float(sy)+rnd(rs))/float(n);\n"
    "    float z=u1; float r=sqrt(max(1.0-z*z,0.0)); float phi=TAU*u2;\n"
    "    vec3 dir=tn*(r*cos(phi))+bt*(r*sin(phi))+nrm*z;\n"
    "    vec3 Li=vec3(0.0), thr=vec3(1.0); vec3 o=pos+nrm*voxel*1.5, d=dir;\n"
    "    for(int b=0;b<=bounces;++b){ vec3 hp; ivec3 hc; vec3 hn;\n"
    "      if(!trace(o,d,1e5,hp,hc,hn)){ Li+=thr*sky; break; }\n"
    /* SDF found the surface; refine into the SVO for the exact material. */
    "      vec3 diff, emis;\n"
    "      if(usesvo==1){ svoMat(hp,diff,emis); } else { diff=mat[2*cidx(hc)].rgb; emis=mat[2*cidx(hc)+1].rgb; }\n"
    "      Li += thr*emis;\n"
    "      Li += thr*diff*direct(hp,hn)*INVPI;\n"           /* Lambertian rho/pi * E. */
    "      thr *= diff;\n"
    "      if(b==bounces) break;\n"
    "      vec3 ct,cb; basis(hn,ct,cb); float a=rnd(rs),e2=rnd(rs);\n"
    "      float cz=sqrt(a), cr=sqrt(1.0-a), cp=TAU*e2;\n"
    "      d=ct*(cr*cos(cp))+cb*(cr*sin(cp))+hn*cz; o=hp+hn*voxel*1.5; }\n"
    "    float y[9]; shb(normalize(dir),y);\n"
    "    for(int c=0;c<3;++c) for(int i=0;i<9;++i) sh[c*9+i]+=Li[c]*weight*y[i]; }\n"
    "  for(int k=0;k<27;++k) osh[27u*li+uint(k)]=sh[k]; }\n";

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
    GLuint prog=make_compute(CS); if(!prog) return 1;

    const int N=48; const float voxel=1.0f; const float origin[3]={0,0,0};
    const size_t cells=(size_t)N*N*N;
    const float lo[3]={8,8,8}, hi[3]={40,40,40};
    float *sdf=malloc(cells*sizeof(float));
    float *mat=malloc(cells*2*4*sizeof(float));
    float lux[8]={24.5f,24.5f,24.5f,0, 0,0,1,0};
    float lights[16]={0};                       /* up to 4 vec4 (1 light). */
    GLuint bo_sdf,bo_mat,bo_lux,bo_out,bo_lit,bo_svo;
    glGenBuffers(1,&bo_sdf); glGenBuffers(1,&bo_mat); glGenBuffers(1,&bo_lux);
    glGenBuffers(1,&bo_out); glGenBuffers(1,&bo_lit); glGenBuffers(1,&bo_svo);
    float outsh[27]; int dims[3]={N,N,N};
    /* Packed SVO node: children[8] + diffuse vec4 + emissive vec4 (64B, lm_gpu_node). */
    typedef struct { uint32_t child[8]; float diffuse[4]; float emissive[4]; } gnode;
    float svomin[3]={0,0,0}, svomax[3]={(float)N,(float)N,(float)N}; int svodepth=8;
    gnode *svo=NULL; int svo_nodes=0;

    #define RUN(sky_,bounces_,nx,ny,nz,nlit,mode_,usesvo_) do{ \
        lux[4]=nx; lux[5]=ny; lux[6]=nz; \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_svo); glBufferData(GL_SHADER_STORAGE_BUFFER,(svo?svo_nodes:1)*(GLsizeiptr)sizeof(gnode),svo,GL_STATIC_DRAW); \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_sdf); glBufferData(GL_SHADER_STORAGE_BUFFER,cells*sizeof(float),sdf,GL_STATIC_DRAW); \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_mat); glBufferData(GL_SHADER_STORAGE_BUFFER,cells*2*4*sizeof(float),mat,GL_STATIC_DRAW); \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_lux); glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof lux,lux,GL_STATIC_DRAW); \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_lit); glBufferData(GL_SHADER_STORAGE_BUFFER,sizeof lights,lights,GL_STATIC_DRAW); \
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
        glUniform1i(glGetUniformLocation(prog,"nlights"),nlit); \
        glUniform1i(glGetUniformLocation(prog,"mode"),mode_); \
        glUniform3fv(glGetUniformLocation(prog,"svomin"),1,svomin); \
        glUniform3fv(glGetUniformLocation(prog,"svomax"),1,svomax); \
        glUniform1i(glGetUniformLocation(prog,"svodepth"),svodepth); \
        glUniform1i(glGetUniformLocation(prog,"usesvo"),usesvo_); \
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,0,bo_sdf); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,1,bo_mat); \
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,2,bo_lux); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,3,bo_out); \
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER,4,bo_lit); glBindBufferBase(GL_SHADER_STORAGE_BUFFER,5,bo_svo); \
        glDispatchCompute(1,1,1); glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT); \
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,bo_out); glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,0,sizeof outsh,outsh); \
    }while(0)

    const float PI=3.14159265f; int fail=0;

    /* open sky=1 -> pi */
    for(size_t i=0;i<cells;++i) sdf[i]=1000.0f;
    for(size_t i=0;i<cells*8;++i) mat[i]=0.0f;
    RUN(1.0f,0,0,0,1,0,0,0); float e_open=sh_irradiance(outsh,0,0,1);

    /* enclosed shell SDF (air pocket, solid outside). */
    for(int z=0;z<N;++z)for(int y=0;y<N;++y)for(int x=0;x<N;++x)
        sdf[(size_t)(z*N+y)*N+x]=-sd_box((float)x+0.5f,(float)y+0.5f,(float)z+0.5f,lo,hi);

    /* MULTI-BOUNCE: emissive E=1, diffuse rho=0.5. Expect pi*(1+rho+..+rho^b). */
    float E=1.0f, rho=0.5f;
    for(size_t i=0;i<cells;++i){ mat[i*8+0]=rho; mat[i*8+1]=rho; mat[i*8+2]=rho;
                                 mat[i*8+4]=E; mat[i*8+5]=E; mat[i*8+6]=E; }
    RUN(0.0f,0,0,0,1,0,0,0); float b0=sh_irradiance(outsh,0,0,1);
    RUN(0.0f,1,0,0,1,0,0,0); float b1=sh_irradiance(outsh,0,0,1);
    RUN(0.0f,3,0,0,1,0,0,0); float b3=sh_irradiance(outsh,0,0,1);
    float exp0=PI*E, exp1=PI*E*(1+rho), exp3=PI*E*(1+rho+rho*rho+rho*rho*rho);

    /* DIRECT-LIGHT PROBE (mode 1), open scene so shadow rays escape. */
    for(size_t i=0;i<cells;++i) sdf[i]=1000.0f;
    /* directional: colour 2, travel down, normal up -> E = 2*cos(0) = 2. */
    lights[0]=0;lights[1]=0;lights[2]=0;lights[3]=1; /* pos, kind=1 dir */
    lights[4]=0;lights[5]=-1;lights[6]=0;lights[7]=0; /* dir=down */
    lights[8]=2;lights[9]=2;lights[10]=2;lights[11]=0; /* colour, cos_inner */
    lights[12]=0;lights[13]=0;lights[14]=0;lights[15]=0;
    RUN(0.0f,0,0,1,0,1,1,0); float e_dir=outsh[0];
    /* point: colour 3 at 5 above, normal up -> E = 3/25 = 0.12. */
    lights[0]=24.5f;lights[1]=29.5f;lights[2]=24.5f;lights[3]=0; /* pos, kind=0 point */
    lux[0]=24.5f;lux[1]=24.5f;lux[2]=24.5f;
    lights[8]=3;lights[9]=3;lights[10]=3;lights[11]=0;
    RUN(0.0f,0,0,1,0,1,1,0); float e_pt=outsh[0];
    /* spot: colour 3 at 5 above, axis down, receiver on-axis -> passes cone = 0.12. */
    lights[3]=2; /* kind=2 spot */
    lights[4]=0;lights[5]=-1;lights[6]=0;lights[7]=0; /* axis down */
    lights[11]=cosf(0.35f); /* cos_inner */
    lights[12]=cosf(0.52f);lights[13]=0;lights[14]=0;lights[15]=0; /* cos_outer */
    RUN(0.0f,0,0,1,0,1,1,0); float e_spot=outsh[0];

    /* --- REAL SVO: octree traversal. 9-node SVO (root + 8 leaves), each octant
     * carries its index as diffuse.r; probe each octant centre (mode 2). --- */
    gnode hand[9]; memset(hand,0,sizeof hand);
    for(int c=0;c<8;++c) hand[0].child[c]=(uint32_t)(1+c);
    for(int k=0;k<8;++k){ for(int c=0;c<8;++c) hand[1+k].child[c]=0xFFFFFFFFu; hand[1+k].diffuse[0]=(float)k; }
    svo=hand; svo_nodes=9; svomin[0]=svomin[1]=svomin[2]=0; svomax[0]=svomax[1]=svomax[2]=8; svodepth=8;
    int svo_octants_ok=1;
    for(int k=0;k<8;++k){ lux[0]=(k&1)*4+2; lux[1]=((k>>1)&1)*4+2; lux[2]=((k>>2)&1)*4+2;
        RUN(0.0f,0,0,0,1,0,2,1);
        if((int)(outsh[0]+0.5f)!=k){ svo_octants_ok=0; printf("  octant %d -> %.1f (expect %d)\n",k,outsh[0],k); } }

    /* --- REAL SVO in the gather: single-leaf SVO (material rho/E) feeds the hit
     * material after the SDF trace + refine; multibounce b1 must match dense. --- */
    gnode leaf[1]; memset(leaf,0,sizeof leaf); for(int c=0;c<8;++c) leaf[0].child[c]=0xFFFFFFFFu;
    leaf[0].diffuse[0]=leaf[0].diffuse[1]=leaf[0].diffuse[2]=rho;
    leaf[0].emissive[0]=leaf[0].emissive[1]=leaf[0].emissive[2]=E;
    svo=leaf; svo_nodes=1; svomax[0]=svomax[1]=svomax[2]=(float)N; svodepth=8;
    for(int z=0;z<N;++z)for(int y=0;y<N;++y)for(int x=0;x<N;++x)
        sdf[(size_t)(z*N+y)*N+x]=-sd_box((float)x+0.5f,(float)y+0.5f,(float)z+0.5f,lo,hi);
    lux[0]=24.5f;lux[1]=24.5f;lux[2]=24.5f;
    RUN(0.0f,1,0,0,1,0,0,1); float b1_svo=sh_irradiance(outsh,0,0,1);

    printf("open(sky=1)      E=%.3f (expect ~pi=%.3f)\n", e_open, PI);
    printf("multibounce b0   E=%.3f (expect %.3f)\n", b0, exp0);
    printf("multibounce b1   E=%.3f (expect %.3f)\n", b1, exp1);
    printf("multibounce b3   E=%.3f (expect %.3f)\n", b3, exp3);
    printf("direct sun       E=%.3f (expect 2.000)\n", e_dir);
    printf("direct point     E=%.3f (expect 0.120)\n", e_pt);
    printf("direct spot      E=%.3f (expect 0.120)\n", e_spot);
    printf("svo octant descent %s\n", svo_octants_ok?"OK":"BAD");
    printf("gather via SVO b1 E=%.3f (expect ~%.3f = dense b1)\n", b1_svo, b1);

    if (fabsf(e_open-PI)>0.35f) fail|=1;
    if (!svo_octants_ok) fail|=128;
    if (fabsf(b1_svo-b1)>0.30f) fail|=256;
    if (fabsf(b0-exp0)>0.35f) fail|=2;
    if (fabsf(b1-exp1)>0.5f) fail|=4;
    if (fabsf(b3-exp3)>0.7f) fail|=8;
    if (fabsf(e_dir-2.0f)>0.02f) fail|=16;
    if (fabsf(e_pt-0.12f)>0.01f) fail|=32;
    if (fabsf(e_spot-0.12f)>0.01f) fail|=64;
    printf("%s%s", fail?"FAILED mask ":"PASSED", fail?"":"\n");
    if (fail) printf("%d\n", fail);

    free(sdf); free(mat);
    SDL_GL_DeleteContext(gc); SDL_DestroyWindow(win); SDL_Quit();
    return fail ? 1 : 0;
}
