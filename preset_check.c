/* Checks the released bank, selection names, and saved-state restoration. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dlfcn.h>
#include "vst2.h"
static intptr_t am(AEffect*f,int32_t o,int32_t i,intptr_t v,void*p,float t) {
    switch(o) {case 1:return 2400; case 23:return 44100; case 24:return 512; default:return 0;}
}
int main(int argc,char**argv) {
    assert(argc >= 2);
    void*h=dlopen(argv[1],RTLD_NOW|RTLD_LOCAL); assert(h);
    VstMainProc entry=(VstMainProc)dlsym(h,"VSTPluginMain"); assert(entry);
    AEffect*fx=entry(am); assert(fx && fx->numPrograms==20 && fx->numParams==17);
    fx->dispatcher(fx,effOpen,0,0,NULL,0);
    if(argc>2) {
        FILE*f=fopen(argv[2],"rb"); assert(f); fseek(f,0,SEEK_END);
        long n=ftell(f)-160; assert(n>0); fseek(f,160,SEEK_SET);
        void*b=malloc(n); assert(fread(b,1,n,f)==n); fclose(f);
        fx->dispatcher(fx,effSetChunk,0,n,b,0); free(b);
    }
    for(int i=0;i<20;i++) {
        char name[128]={0}, current[128]={0};
        fx->dispatcher(fx,effSetProgram,0,i,NULL,0);
        assert(fx->dispatcher(fx,effGetProgram,i,0,NULL,0)==i);
        fx->dispatcher(fx,effGetProgramNameIndexed,i,0,name,0);
        fx->dispatcher(fx,effGetProgramName,0,0,current,0);
        assert(strcmp(name,"Init") && name[0] && !strcmp(name,current));
        printf("%d %s",i,name);
        for(int j=0;j<17;j++) printf(" %.9g",fx->getParameter(fx,j));
        puts("");
    }
    fx->dispatcher(fx,effSetProgram,0,7,NULL,0);
    fx->setParameter(fx,1,0.314f);
    void*data=NULL; long n=fx->dispatcher(fx,effGetChunk,0,0,&data,0); assert(n>0);
    void*saved=malloc(n); memcpy(saved,data,n);
    fx->setParameter(fx,1,0.9f);
    fx->dispatcher(fx,effSetChunk,0,n,saved,0);
    /* Bank chunks do not carry a program index; the host restores that separately. */
    fx->dispatcher(fx,effSetProgram,0,7,NULL,0);
    assert(fx->getParameter(fx,1)==0.314f);
    free(saved);
    fx->dispatcher(fx,effClose,0,0,NULL,0);
    return 0;
}
